# RMUC 2027 V1.0.0 17 mm heat source audit

Audit date: 2026-11-05 UTC. This is a pinned-source audit, not a complete referee implementation or latest-Q&A certification. Companion data: `heat_source_audit.json`.

## Result and implementation boundary

**The 2027 manual prescribes heat-triggered launcher lockout, not excess-heat HP damage.** The heading “扣血与超限惩罚机制” does not mean every subsection deducts HP. Do not import a previous season's overheat damage formula, add a heat penalty to the attack ledger, or automatically issue a yellow card.

The source-backed 17 mm core is: each detected shot adds 10; cooling settles every 100 ms by effective cooling-per-second / 10; heat is floored at zero; temporary heat lock lasts until zero; a higher threshold causes round-long firing lock. Exact equality at the higher threshold is **conflicting** in the manual. A heat profile, cooling phase, and same-time event policy are also required before a deterministic integration can claim a fully determined result.

The existing tactical actors retain synthetic HP100, DAMAGE10, and 60 Hz physics. Borrowing infantry level-1 cooling-priority values (heat limit 40, base cooling 12/s) is an explicitly named experimental heat fixture, not proof these actors implement official infantry. The heat adapter must not silently change HP, hit damage, collision, armor detection, or the existing attack-damage settlement. It may map its actually emitted synthetic projectile to a detected shot only under experimental provenance. Trigger intent, blocked requests, and target impacts are not additional heat events.

## Source, checksum, and verification

- Source: **RoboMaster 2027 机甲大师超级对抗赛比赛规则手册 V1.0.0（20261015）**.
- [Official source hub](https://bbs.robomaster.com/wiki/20204847/1969149?source=7).
- [Pinned PDF](https://bbs-web-static.robomaster.com/451e5737ba5d4463909a3013456a4e551792055647969/RoboMaster%202027%20%E6%9C%BA%E7%94%B2%E5%A4%A7%E5%B8%88%E8%B6%85%E7%BA%A7%E5%AF%B9%E6%8A%97%E8%B5%9B%E6%AF%94%E8%B5%9B%E8%A7%84%E5%88%99%E6%89%8B%E5%86%8CV1.0.0%EF%BC%8820261015%EF%BC%89.pdf).
- Local source: `/workspace/shared/rmuc2027/rmuc_2027_cn_v1_0_0.pdf`.
- Independently recomputed PDF SHA-256: `9e2eed9295536c6fd547b62d634df1d5e70a2e1e2250e5dff3521d557e5d54be`; 22,455,861 bytes, 144 physical pages.
- Derived matrix SHA-256: `f652d2f7c5fd9e88af660ad8fc46baa7ce8b36e4480dd6f18c43860466cecdea`.
- All references below use **printed pages**. Physical PDF page = printed page + 1. Extracts: `/workspace/shared/rmuc2027/pages/printed_NNN.txt`.
- Rendered with Poppler and visually inspected: printed pp17, 19, 26, 64-67, 71, 74-75, 89, 91, 93-94, 98-99, 104-107, 109-110, 123-125. The p66/p67 inequality conflict and p93/p94 table values were checked in the pixels, not merely extracted text.
- Companion JSON verifies 94 quoted fragments after whitespace normalization and removal of two identified intruding row labels; all 20 infantry heat-profile rows were independently matched to extracted rows and visually checked.
- The matrix is a derived index, not an authority. Latest official Q&A, referee communications/firmware, and the robot construction manual were **not newly checked**. Do not claim that an unresolved point was settled by those sources.

## 1. Detected shots, cooling, and the lock state machine

### 1.1 Detected firing is the heat input

**§2 table 2-1, p17 / PDF p18** (`H-01`):

> 机器人发出弹丸或飞镖的行为。后文“比赛机制”章节中提到的“发射”以裁判系统检测到发射行为为准。
>
> 若发射机构锁定，则其处于断电状态。
>
> 若发射机构解锁，则根据其允许发弹量情况判定上电或断电。

**§5.1.3, p66 / PDF p67** (`H-02`, `H-03`; matrix `HEAT_ACCUMULATION`):

> 设定机器人的射击热量上限为 Q0，当前射击热量为 Q1，裁判系统每检测到一发 17mm 弹丸，当前射击热量 Q1 增加 10（与 17mm 弹丸的射击初速度无关）。
>
> 射击热量按 10Hz 的频率结算冷却，每个检测周期热量冷却值=每秒冷却值/10。

Thus a detected launch that misses, or later fails armor-hit detection, still creates heat. A rejected fire command is not a launch. Hit speed, impact damage, and attack-ledger credit are not heat inputs. The documented 42 mm increment is 100 and its higher threshold is Q0+200; these must not leak into a 17 mm profile. The p108 installation confirmation-shot heat exception applies to 42 mm only (`H-36`).

### 1.2 Temporary lock, exact lower boundary, and release

**§5.1.3 p66** (`H-04`, `H-05`):

> 地面机器人的射击热量超限时，该机器人的发射机构会被锁定，射击热量超限导致的锁定和解锁与其他情况导致的锁定和解锁相互独立。
>
> 当 Q2>Q1>Q0，该机器人对应操作手的第一视角操作界面图传画面断开，且发射机构会被锁定，直到 Q1=0，第一视角操作界面才会恢复正常，且发射机构解锁。

The source says **Q1>Q0**, not Q1>=Q0. Q1=Q0 does not newly create heat lock. The shot that crosses Q0 is first detected and adds heat; a predictive “never permit a shot that would exceed Q0” budget is not the official mechanism. After crossing, the temporary reason is latched until Q1=0, even when cooling has already brought heat below Q0.

**Figure 5-1 p67** (`H-07`, `H-08`) places `Q1 = Q1 + 10` before the threshold checks. Its cooling branch is `100ms检测周期到` -> `Q1 = Q1 – 每秒冷却值 / 10` -> `Q1 ≤ 0` -> `Q1=0，机器人发射机构解锁，图传画面恢复正常`.

Use a lower clamp of zero, not an upper clamp at Q0 or Q2. An upper clamp would erase the condition the rule is meant to detect. Releasing the temporary heat reason must not release overspeed, disconnected-speed-module, weak-state, death-state, permanent-heat, or other independent restrictions. Launcher unlock also does not guarantee power when ammunition allowance is unavailable.

### 1.3 Permanent threshold conflict and permanence

**§5.1.3 p66** (`H-06`):

> 当 Q1>Q2，该机器人在当局后续全部时间发射机构锁定，且不再解锁。
>
> 对于 17mm 发射机构，Q2=Q0+100；

**Figure 5-1 p67** (`H-07`) instead checks **Q1 ≥ Q2**, leading to:

> 该机器人在当局后续全部时间发射机构锁定，且不再解锁

This is a real source conflict. For a fresh threshold decision:

| Heat after detected shot | Verified consequence |
| --- | --- |
| Q1 <= Q0 | No new heat lock; an already latched temporary reason may still remain |
| Q0 < Q1 < Q0+100 | Temporary firing lock and first-person video outage until heat zero |
| Q1 = Q0+100 | Unresolved: diagram permanent, prose leaves equality outside both written intervals |
| Q1 > Q0+100 | Permanent firing lock for the rest of the round |

A “prose >” engineering mode that puts equality into the temporary branch supplies a missing equality rule itself. It is not a fully specified official prose interpretation. A strict module may return unresolved at equality; a diagram-based or other engineering mode must carry a policy ID into configuration, state hashes, reports, and replay. Permanent means **rest of round**, so ordinary cooling to zero, death heat reset, or an unrelated unlock must not erase its latch.

### 1.4 Video is narrower than all UI; permanent-only video is unspecified

**p67 note** (`H-09`):

> 因超热量导致图传画面断开时，小地图、自定义 UI 仍可正常使用。

Heat rules do not disable the chassis, blank the minimap, or disable all custom UI. The native tactical map is not an official first-person video feed; leaving video effects explicitly unsupported is preferable to inventing a map blackout.

The **direct permanent branch** names launcher lock only and bypasses the temporary/video branch in the figure. It does not explicitly establish a new video cutoff (`H-37`, `U-PERMANENT-VIDEO`). If video is modeled, preserve an already active temporary heat/video reason until zero; report permanent-only video behavior as unspecified or use a labeled engineering policy. Do not advertise a permanent video blackout as verified.

## 2. Official timing versus engineering scheduling

The official period is 100 ms; the 60 Hz tactical tick is not an official heat-settlement frequency. Six exact 60 Hz ticks equal 100 ms, but choosing the initial tick/clock epoch is still a scheduling assumption. Do not continuously subtract cooling/60 and call that the documented discrete behavior.

The visual flowchart has separate shot and cooling branches. It establishes addition then threshold evaluation **within a detected-shot event**, and subtraction then zero handling **within a cooling cycle**. It does **not** establish (`H-10`; matrix `HEAT_EVENT_ORDER`):

- Order of a shot and a cooling cycle at exactly the same time.
- Ordering of several shots recorded at the same instant, nor detector-to-power-off latency or already-emitted projectiles.
- Order of a shot's XP/upgrade, effective cooling/profile change, heat check, death, or independent lock changes at the same time.
- First cooling phase relative to match start or whether an event is represented at command, muzzle, sensor, or telemetry arrival time.

For example, with Q0=40, current heat32 and C=30, shot-first produces heat42 and a latched temporary lock even if cooling then gives39; cooling-first yields29 then39 and no lock. Determinism alone does not make either order official. The planned tactical cooling-before-gate/shot order may be a named experimental rule. Replays must reproduce its phase and ordering rather than silently selecting a new policy.

## 3. Heat limit and cooling require an explicit robot profile

### 3.1 Infantry: launcher type and level

**§5.5.1 p89** (`H-16`):

> 比赛开始时，步兵机器人的等级为 1 级，可通过增加经验值实现等级提升，其余机器人均不适用经验体系。

**§5.5.2 p91** (`H-17`):

> 三分钟准备阶段开始后，步兵机器人的操作手可选择机器人的底盘和发射机构类型，其他机器人均不适用性能体系。
>
> 七分钟比赛阶段开始后且机器人底盘和发射机构类型选择完毕后，整局比赛期间不可更换。
>
> 若不选择底盘或发射机构类型，则在七分钟比赛阶段开始后，底盘类型默认为“血量优先”，发射机构类型默认为“冷却优先”。

**Table 5-18 pp93-94 / PDF pp94-95** (`H-18`, `H-19`), visually verified in full:

| Level | Burst-priority Q0 | Burst cooling/s | Cooling-priority Q0 | Cooling-priority cooling/s |
| --- | ---: | ---: | ---: | ---: |
| 1 | 170 | 5 | 40 | 12 |
| 2 | 180 | 7 | 48 | 14 |
| 3 | 190 | 9 | 56 | 16 |
| 4 | 200 | 11 | 64 | 18 |
| 5 | 210 | 12 | 72 | 20 |
| 6 | 220 | 13 | 80 | 22 |
| 7 | 230 | 14 | 88 | 24 |
| 8 | 240 | 16 | 96 | 26 |
| 9 | 250 | 18 | 104 | 28 |
| 10 | 260 | 20 | 120 | 30 |

The detailed tables make launcher type and level the heat inputs; chassis HP/power selection is independent. Do not derive Q0 from synthetic HP100 or use a constant common to all 17 mm robots. The official infantry default can be borrowed only after explicitly identifying the fixture's intended heat profile, not by silently identifying the whole synthetic actor as infantry.

### 3.2 Sentry: control mode, posture, and exceptions

**§3.1.3 table 3-6 p26** (`H-20`):

> 不适用经验与性能体系，性能与机器人操作方式相关，具体如下：

- Automatic sentry: Q0=260, base cooling30/s, initial/max HP400.
- Semi-automatic sentry: Q0=100, base cooling10/s, initial/max HP200.

These are **base** values. **§5.7.2 table 5-28 pp109-110** (`H-21`, `H-22`) says the initial posture is movement, not attack. Defense and movement reduce cooling to 1/3 **after** choosing the applicable cooling buff. Attack gives x3 normally, x2 after degradation. Enhanced attack states:

> 当前热量始终为 0，发射弹丸不增加热量

The same page states:

> 当效果为热量冷却与底盘功率下降时，其在哨兵机器人已完成热量冷却增益和底盘功率增益计算的值的基础上乘算。
>
> 上述计算均取整数值。

The rounding direction is unspecified. Auto-sentry unbuffed movement cooling is exactly10/s; semi-auto's10/3 requires that unresolved integer policy. Sentry switching cooldown is5s; a posture degrades after cumulative time **exceeds3 minutes**; each enhanced posture has independent15s total allowance. None is a general 17 mm firing-rate constant. The enhanced-attack zero-heat exception does not authorize clearing an already established rest-of-round permanent lock.

## 4. Effective cooling and rounding dependencies

**§5.6.4.1 p105** (`H-23`):

> 当机器人获得的同类增益超过一个时，取最大增益效果，包括攻击、防御、易伤、回血和射击热量冷却增益。

Known heat-relevant modifiers in the pinned source:

- Large rune, table5-22 p98: average rings[1,3] no cooling boost; (3,7] x2; (7,8] x2; (8,9] x3; (9,10] x5. Table5-23 pp98-99 gives30/35/40/45/50/60 seconds for5/6/7/8/9/10 activated arms (`H-24`).
- Through-tunnel route, table5-27 p106: detect end, interior, opposite end in required order, then x1.5 for120s. Merely crossing the two ends is a different effect (`H-25`).
- Own fortress, §5.6.4.9 p107: only when own outpost position is not controlled by own team, eligible activation supplies additive `w=min(75,floor(Delta/80))`, where Delta is max base HP minus the lowest base HP in the round. Compare final cooling rates with other buffs, do not add all effects (`H-26`). The source's exact example is `40*5>40+75`, hence200/s.
- Sentry reduction applies after selecting the strongest cooling result. The source pp109-110 example takes max(30*3,30+75)=105, then105/3=35/s.

Buff persistence matters at death: table5-26 p104 and p106 clear terrain temporary effects on death/expiry, while non-field temporary effects such as rune/posture are not erased solely by death. Continuous field effects end when activation ends (`H-27`). A bounded static infantry fixture can explicitly leave these features out rather than emulate all buffs.

**No general integer rounding of heat or cooling is specified.** The base level1 cooling-priority rate12/s produces1.2 per cycle; burst5/s produces0.5. Keep fractional heat accurately. Rounding1.2 to1 every cycle changes the official cooling rate. A tenths representation suffices for these base tables but is not a complete solution to every buff:5/s times1.5 becomes0.75 per100ms. Sentry's integer instruction needs a separately declared rounding direction for nonintegral results.

The p105 line `经过增益计算后的最终伤害值或回血值若非整数，则四舍五入。` and p17 HP-settlement rounding govern **damage/healing/HP**, not heat. Do not generalize them (`H-28`).

## 5. Speed, sensor detection, and other restrictions

Infantry and sentry muzzle limit is25m/s, tables3-5/3-6 pp24-25. **§5.1.2 table5-3 p66** (`H-29`) uses delta=V1-V0 for17mm:

- 0<delta<5: lock15s.
- 5<=delta<10: lock20s.
- delta>=10: lock for the rest of the round.
- V1=V0 is not overspeed. Lock times do not accumulate; lock/unlock is independent from heat.

The detected overspeed shot still adds10 heat because §5.1.3 explicitly makes heat independent of muzzle speed. The manual does not specify a speed-to-heat scaling or cancellation of heat when an overspeed penalty occurs.

**§5.1.1 pp64-65** (`H-30`, `H-31`) requires17mm armor impacts with **normal-component speed>12m/s**, plus minimum armor-hit detection interval50ms. That is target armor-detection behavior, not a20shots/s limit on heat events or a reason to omit launch heat. A shot can add heat without valid impact damage. The source's raw robot-armor17mm damage20 differs from sandbox DAMAGE10; heat integration does not authorize changing that legacy parameter.

**§5.1.5.3 p71** (`H-32`) independently locks a launcher when its corresponding speed module is offline; reconnection releases that reason only. The flowchart shows500ms offline checks while prose says immediately. This audit does not implement or resolve sensor-offline latency. Neither cooling to0 nor reconnecting a sensor may clear the other cause.

## 6. Death, revival, and upgrades

**§5.3.2 p74** (`H-33`; matrix `REV-01`):

> 机器人战亡时，其射击热量将被重置为 0，缓冲能量将被重置为缓冲能量上限。

Reset heat at **death**, not later at revival. Combining this with the temporary rule “直到Q1=0” allows the **temporary heat** condition to release at zero; it does not restore the whole dead robot. Permanent firing lock explicitly lasts for the rest of the round and therefore survives the heat reset. Other lock reasons must survive as applicable.

**p75** (`H-34`) retains level and XP through either revival method, and separately states that a weak-state robot's launcher is locked. A robot revived at zero heat is not automatically permitted to shoot. The current tactical sandbox does not implement official revival; documenting death heat reset does not add that lifecycle.

**§5.5.1 p89** says infantry earns1XP per fired projectile; **§5.5.2 p91** says:

> 机器人升级后，血量上限提升的部分将被立即添加至机器人的当前血量。

The latter is an HP rule, not a heat-reset rule. Table5-18 changes Q0 and base cooling by level, but the inspected source does not specify simultaneous shot-XP-upgrade-heat-check ordering, a heat zero/rescale on upgrade, or cooling-phase reset at a profile change (`H-35`). Do not invent these transitions. Restrict a first implementation to a declared static profile or label any dynamic-transition policy.

A separate explicit “round starts at heat0” sentence was not found in the searched heat passages. An externally supplied initial state or a named experimental zero-initialized fixture avoids overstating the source. Death-zero is explicit and not ambiguous.

## 7. HP penalties and attack-damage attribution are separate

No heat HP-loss formula appears in §5.1.3; the full extracted text's heat mentions were searched and no such formula was found (`H-11`). Heat events must not mutate HP or add attack damage. This absence finding concerns the pinned source, not hypothetical later clarifications.

**§2 table2-1 p19** (`H-12`):

> 注：一方机器人、基地、前哨站因违规判罚导致的扣血计入对方攻击伤害。
>
> 非攻击伤害：一方机器人因装甲模块受撞击、裁判系统模块离线等导致的血量扣除。

**§7.1.1 p123** groups much of §5.1 under “裁判系统自动判罚”; that label does not supply an HP deduction or override p19's non-attack exclusions. Do not map a generic `penalty` enum automatically to attack score.

Separately adjudicated yellow cards (§7.1.2 table7-2 p124) deduct15% max HP from offender and5% from other living teammates, with further repeated-card rules, and have a1HP floor. Their actual settled HP deduction is credited to the opponent under p19. A yellow card at5HP that settles at1HP contributes4, not the nominal penalty. Yellow-card UI遮挡 also disables minimap and custom UI, unlike heat video outage (`H-13`). Ground-robot red cards p125 set HP0 and bar revival; they are separate violations, not heat-threshold consequences (`H-14`).

The existing `ATTACK_DAMAGE_SOURCES.md` details warning escalation, administrative overrides, and ledger boundaries. Reuse settled deductions rather than duplicating HP arithmetic. A heat module should generate **no deduction event at all** from ordinary heating/lock/cooling, so it cannot accidentally create attack credit or a death.

## 8. Bounded acceptance checks and non-claims

These are review criteria, not a claim that tests or implementation were run in this audit:

1. Known accepted17mm shot adds10 once; blocked fire adds0; impact handling adds0 additional heat.
2. Q0=40,C=12 fixture retains fractional1.2/cycle; three100ms cycles from10 yield6.4. Heat0.4 cooled by1.2 clamps0.
3. A fresh Q1=40 does not lock; an accepted crossing shot makes50 and temporary-locks. A latched lock still holds at39 or1 and releases only at0.
4. Q2=140 equality is unresolved in strict mode or records a named experimental resolution; above140 permanently locks.
5. Cooling/death to0 cannot erase permanent or independent lock reasons. Direct permanent-only video must not be silently asserted official.
6. Heat changes produce no HP change, no attack-ledger credit, no yellow/red card, and no chassis disablement.
7. At a cooling/shot tie, the selected engineering order and phase survive replay; a reordered replay cannot pass as the same state.
8. Profile selection is explicit; borrowed40/12 does not derive from HP100 or imply full infantry, buffs, upgrades, ammo, speed-detection, or revival support.
9. Snapshot, hash, replay, command validation, and policy observations should consistently represent whichever supported heat state is added. These are engineering requirements, not additional official rules.
10. Preserve all legacy mechanics and keep `official:false`; publish neither full-referee equivalence nor latest-Q&A compliance.

The companion JSON separates verified source statements, source conflicts, unspecified dependencies, and the proposed experimental adapter boundary. Only this Markdown file and that JSON file were authored by the source-audit task.
