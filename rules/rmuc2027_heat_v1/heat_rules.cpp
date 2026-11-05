#include "heat_rules.hpp"
#include <limits>
namespace rmuc2027::heat_v1 {
std::string_view error_name(Error e) {
    switch(e) {
    case Error::InvalidConfig:return "InvalidConfig";
    case Error::NegativeValue:return "NegativeValue";
    case Error::NonIncreasingTime:return "NonIncreasingTime";
    case Error::IntegerOverflow:return "IntegerOverflow";
    case Error::UnadjudicatedThresholdEquality:return "UnadjudicatedThresholdEquality";
    case Error::UnadjudicatedShotCoolingCoincidence:return "UnadjudicatedShotCoolingCoincidence";
    case Error::UnadjudicatedShotDeathCoincidence:return "UnadjudicatedShotDeathCoincidence";
    }
    return "InvalidError";
}
std::string_view policy_name(ThresholdPolicy p) {
    switch(p) {
    case ThresholdPolicy::StrictRejectUnadjudicated:return "strict-reject-unadjudicated";
    case ThresholdPolicy::EngineeringProseStrictGreaterV1:return "ENGINEERING-prose-greater-temporary-at-equality-v1";
    case ThresholdPolicy::EngineeringFlowchartGreaterEqualV1:return "ENGINEERING-flowchart-greater-equal-v1";
    }
    return "invalid";
}
std::string_view policy_name(BoundaryPolicy p) {
    switch(p) {
    case BoundaryPolicy::StrictRejectUnadjudicated:return "strict-reject-unadjudicated";
    case BoundaryPolicy::EngineeringCoolingBeforeShotV1:return "ENGINEERING-cooling-before-shot-v1";
    case BoundaryPolicy::EngineeringShotBeforeCoolingV1:return "ENGINEERING-shot-before-cooling-v1";
    }
    return "invalid";
}
Result<Heat> Heat::start(Config c) {
    if(c.limit_tenths<0 || c.limit_tenths>std::numeric_limits<std::int64_t>::max()-PERMANENT_MARGIN_TENTHS ||
       c.cooling_per_second<0 ||
       (c.threshold!=ThresholdPolicy::StrictRejectUnadjudicated && c.threshold!=ThresholdPolicy::EngineeringProseStrictGreaterV1 && c.threshold!=ThresholdPolicy::EngineeringFlowchartGreaterEqualV1) ||
       (c.boundary!=BoundaryPolicy::StrictRejectUnadjudicated && c.boundary!=BoundaryPolicy::EngineeringCoolingBeforeShotV1 && c.boundary!=BoundaryPolicy::EngineeringShotBeforeCoolingV1) ||
       (c.death!=DeathPolicy::StrictRejectCoincidentShot && c.death!=DeathPolicy::EngineeringShotBeforeDeathResetV1))
        return Error::InvalidConfig;
    return Heat(c);
}
Result<Snapshot> Heat::apply(Event e) {
    if(e.time_ms<0 || e.detected_shots<0) return Error::NegativeValue;
    if(e.time_ms<=state_.last_event_ms) return Error::NonIncreasingTime;
    const auto max=std::numeric_limits<std::int64_t>::max();
    if(e.detected_shots>max/SHOT_HEAT_TENTHS || state_.detected_shots>max-e.detected_shots)
        return Error::IntegerOverflow;
    const bool boundary=e.time_ms>0 && e.time_ms%COOLING_PERIOD_MS==0;
    if(boundary && e.detected_shots && config_.boundary==BoundaryPolicy::StrictRejectUnadjudicated)
        return Error::UnadjudicatedShotCoolingCoincidence;
    if(e.died && e.detected_shots && config_.death==DeathPolicy::StrictRejectCoincidentShot)
        return Error::UnadjudicatedShotDeathCoincidence;
    auto next=state_;
    const auto cool=[&](std::int64_t cycles) {
        // C heat/s divided by10, then expressed in tenths, is exactly C.
        // Saturate safely without multiplying an unbounded interval by C.
        if(cycles>0 && config_.cooling_per_second>0) {
            const auto needed=next.heat_tenths/config_.cooling_per_second+
                              (next.heat_tenths%config_.cooling_per_second!=0);
            if(cycles>=needed) next.heat_tenths=0;
            else next.heat_tenths-=cycles*config_.cooling_per_second;
        }
        if(next.heat_tenths==0) next.overheat_locked=false;
    };
    const auto cycles=e.time_ms/COOLING_PERIOD_MS-state_.time_ms/COOLING_PERIOD_MS;
    const bool shot_first=boundary && e.detected_shots && config_.boundary==BoundaryPolicy::EngineeringShotBeforeCoolingV1;
    cool(cycles-(shot_first ? 1 : 0));
    if(e.detected_shots) {
        const auto gain=e.detected_shots*SHOT_HEAT_TENTHS;
        if(next.heat_tenths>max-gain) return Error::IntegerOverflow;
        const auto before=next.heat_tenths;
        const auto permanent=config_.limit_tenths+PERMANENT_MARGIN_TENTHS;
        // A detected batch is a sequence of +10 rounds, not one giant round.
        // Reject an unresolved intermediate equality, too, without a loop over
        // untrusted shot counts. No cooling is interleaved within this batch.
        if(config_.threshold==ThresholdPolicy::StrictRejectUnadjudicated && permanent>before &&
           (permanent-before)%SHOT_HEAT_TENTHS==0 &&
           (permanent-before)/SHOT_HEAT_TENTHS<=e.detected_shots)
            return Error::UnadjudicatedThresholdEquality;
        next.heat_tenths+=gain;
        next.detected_shots+=e.detected_shots;
        const auto first_temporary=before>config_.limit_tenths ? 1 :
            (config_.limit_tenths-before)/SHOT_HEAT_TENTHS+1;
        if(first_temporary<=e.detected_shots) {
            const auto intermediate=before+first_temporary*SHOT_HEAT_TENTHS;
            if(intermediate<permanent || (intermediate==permanent &&
               config_.threshold==ThresholdPolicy::EngineeringProseStrictGreaterV1))
                next.overheat_locked=true;
        }
        if(next.heat_tenths>permanent || (next.heat_tenths==permanent &&
           config_.threshold==ThresholdPolicy::EngineeringFlowchartGreaterEqualV1))
            next.permanent_latched=true;
    }
    if(shot_first) cool(1);
    // Death resets heat, not the rest-of-round firing latch. This module owns
    // no speed/death/offline firing or video reason and therefore cannot clear it.
    if(e.died) { next.heat_tenths=0; next.overheat_locked=false; }
    next.time_ms=e.time_ms; next.last_event_ms=e.time_ms;
    state_=next;
    return next;
}
} // namespace rmuc2027::heat_v1
