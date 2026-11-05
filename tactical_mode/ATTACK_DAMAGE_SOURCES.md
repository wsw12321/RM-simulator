# RMUC 2027 V1.0.0 attack-damage source audit

Audit date: 2026-11-05 UTC. This is a source/integration audit, not a claim of complete official referee equivalence.

## Finding and safe implementation boundary

A ledger may sum **already-settled qualifying HP deductions** and feed the existing timeout comparison. The official definition is not a sum of projectile hit amounts and is not every decrease in HP. Violation-penalty deductions count for the opposing team; ordinary collision and module-offline deductions do not. At timeout, compare control first, then attack damage, then surviving robots' current HP, then draw.

The present two-robot sandbox can supply **experimental opposing-projectile deductions aggregated per victim per tick**, because every supported hit against a victim belongs to the one opposing actor. Reconcile its aggregate nominal hits with the actual pre/post HP deduction, then credit that one opposing team. This avoids inventing individual-hit overkill allocation. This adapter remains experimental: its geometry, 10-point hits, 100 HP, 60 Hz detection, simultaneous commit, and lack of armor/buffs/referee detection are not official rules. The external ledger is source-backed only to the extent its input facts have genuinely been settled upstream.

Friendly/unknown-source score recipients, moving-dart group-damage score attribution, generic simultaneous event ordering, and official overkill/per-hit/XP settlement are not silently resolved here. An unavailable ledger is not a zero ledger. Keep `official: false` for the tactical mode.

## Pinned source and verification

- Official manual: **RoboMaster 2027 机甲大师超级对抗赛比赛规则手册 V1.0.0（20261015）**.
- [Official source hub](https://bbs.robomaster.com/wiki/20204847/1969149?source=7).
- [Pinned official PDF](https://bbs-web-static.robomaster.com/451e5737ba5d4463909a3013456a4e551792055647969/RoboMaster%202027%20%E6%9C%BA%E7%94%B2%E5%A4%A7%E5%B8%88%E8%B6%85%E7%BA%A7%E5%AF%B9%E6%8A%97%E8%B5%9B%E6%AF%94%E8%B5%9B%E8%A7%84%E5%88%99%E6%89%8B%E5%86%8CV1.0.0%EF%BC%8820261015%EF%BC%89.pdf).
- Local PDF: `/workspace/shared/rmuc2027/rmuc_2027_cn_v1_0_0.pdf`; 22,455,861 bytes, 144 physical pages.
- PDF SHA-256, independently recomputed: `9e2eed9295536c6fd547b62d634df1d5e70a2e1e2250e5dff3521d557e5d54be`.
- Matrix SHA-256 at audit: `f652d2f7c5fd9e88af660ad8fc46baa7ce8b36e4480dd6f18c43860466cecdea`.
- All page numbers below are **printed pages**. Physical PDF page = printed page + 1. Page text is `/workspace/shared/rmuc2027/pages/printed_NNN.txt`.
- Relevant PDF pages 19, 89, 111, 114, and 124 were rendered and visually checked, including table-column boundaries. The companion `attack_damage_source_audit.json` has 17 quotes, verified against the local page text with whitespace normalization and removal of table labels that interrupt extracted sentences.
- The matrix is a useful derived index, not the authority. Latest official Q&A, communication protocols, and referee implementation were **not** newly verified in this audit. Absence findings below apply to the pinned source, not every possible official clarification.

## 1. What counts as attack damage

### 1.1 Definition, qualifying targets, and non-attack loss

**§2, table 2-1, p19 / PDF p20**; matrix `SCORE-01`; audit `AD-01`–`AD-03`:

> 攻击伤害：一方机器人或基地、前哨站受到弹丸、飞镖打击和空中机器人冲击产生的血量扣除。
>
> 注：一方机器人、基地、前哨站因违规判罚导致的扣血计入对方攻击伤害。
>
> 非攻击伤害：一方机器人因装甲模块受撞击、裁判系统模块离线等导致的血量扣除。

Verified implications:

- Qualifying targets include robots, bases, and outposts. The later remaining-HP tie-break has a narrower target scope.
- Recognized bullets, darts, and aerial impacts are attack categories. Ordinary armor collision is not the same as recognized aerial impact.
- A violation-penalty deduction has an explicit score recipient: the victim team's opponent, even with no shooter.
- Ordinary armor-collision and module-offline losses are excluded. Do not infer score from all HP loss, or from a generic enum merely named `penalty`.
- A raw HP difference across a long interval may combine damage, healing, respawn, max-HP changes, and administrative changes. Only an isolated settled deduction or a fully classified event ledger can supply this statistic.

### 1.2 Nominal damage versus actual deduction; overkill limit

The definition says **血量扣除**, not nominal hit strength. A ledger that accepts an externally settled loss should add that loss once. For an isolated adjudicated event with HP 5 before and HP 0 after, its submitted actual deduction is 5; a nominal 20-point hit does not justify entering 20 as that event's actual HP deduction.

However, this is **not an explicit official overkill algorithm**. The reviewed manual does not specify a separate general formula for score overkill, allocation among simultaneous impacts, or overkill XP. `EXP_UNKNOWN_KILL_AMBIGUITIES` explicitly leaves XP overkill versus remaining-HP damage unresolved. Do not promote a chosen `min(pre_hp, nominal_damage)` upstream calculation, projectile iteration order, kill-credit choice, or proportional splitting policy to verified official semantics.

For the existing synthetic opposing-projectile-only tick, the simulator already computes the actual aggregate loss as `pre_hp - post_hp`, equal to `min(pre_hp, sum_nominal_hits)` because there is no simultaneous healing or other damage class. An adapter can validate this equality and credit that aggregate to the single opposing team. No individual-hit assignment is needed for team score. If multiple causes or different score recipients enter the same deduction, require classified settlement facts or leave the score unresolved.

### 1.3 Rounding, modifiers, and detection are upstream

**§2, p17 / PDF p18** (`AD-06`):

> 注：裁判系统在结算血量时进行四舍五入，保留整数。

**§5.6.4.1, p105 / PDF p106**; `buff.stacking_rounding`, `AD-07`:

> 经过增益计算后的最终伤害值或回血值若非整数，则四舍五入。

A settled-loss ledger must not repeat rounding, buffs, or damage application. **§5.1.1 table 5-2, p65** gives default 17 mm / 42 mm raw robot-armor damage of 20 / 200, not the sandbox's 10. **§5.2 p72** modifies projectile raw damage by armor-region multipliers; **§5.6.4.1 p103–105** describes attack/defense/vulnerability effects. These are different from the eventual actual loss entered in a ledger.

**§5.1.1 p64–65** also requires valid detection and specifies minimum detection intervals of 50 ms for 17 mm and 200 ms for 42 mm. Detector scope and mixed-caliber suppression are not fully specified. Aerial detection thresholds remain marked 待定 in the manual. These gates cannot be claimed implemented by circle collision at 60 Hz.

## 2. Friendly and unknown-source damage

**§5.5.1 table 5-14, p89 / PDF p90**; `EXP_DAMAGE_REDISTRIBUTION`, `AD-08`:

> 当一方机器人、基地或前哨站受到攻击伤害，但裁判系统未检测到伤害具体来源，伤害来源不能获取经验或伤害来源为判罚/己方机器人：若为弹丸伤害，经验将平均分给另一方的能够正常造成该类型弹丸伤害且能够获得经验的所有存活机器人；若为非弹丸伤害，经验将平均分给另一方此时所有存活的步兵机器人。

This is in the **经验获取与结算机制表**. It verifies that friendly/unknown-source damage can exist and describes redistribution of **experience**, not a standalone team attack-score recipient rule. The ordinary identified opposing projectile case has one unambiguous attacking team. The friendly/unknown-source case does not gain a verified score recipient merely because XP is given to the other side.

Safe integration:

- Keep shooter/source identity separate from score recipient and from XP recipients.
- The narrowly supported tactical adapter accepts identified opposing-projectile loss only.
- A generic external ledger may accept a resolved score-recipient fact if it records that attribution as an external adjudication; it must not claim the PDF itself supplied that missing choice.
- Otherwise, friendly or unknown-source attack attribution remains unresolved. Do not drop the loss silently, count it for the shooter automatically, or reuse XP redistribution as proof.

## 3. Ordinary collision, offline loss, and penalty distinctions

**§5.1.1 p65 / PDF p66**; `DAMAGE_PROHIBITED_COLLISION`, `AD-09`:

> 地面机器人、基地、前哨站的装甲模块受到撞击时有概率被判定为弹丸伤害。除空中机器人冲击外，不允许通过撞击（包括与机器人冲撞，抛掷物体等）的方式对对方造成伤害。

Thus physical contact, official detected/adjudicated damage category, and a later violation penalty are three different facts. A ledger consuming an externally adjudicated `ordinary_collision` excludes it; it should not invent a sensor misclassification or automatically transform any contact into a card. A real reported collision-as-projectile case requires the applicable referee/event classification to be resolved.

**§5.1.5.1 table 5-4, p69 / PDF p70** states ground-robot main-controller offline effects:

> 发射机构（如有）、云台、底盘断电，每秒扣除上限血量的 5%，直至为 0
>
> 机器人不再检测撞击和弹丸击打造成的伤害和模块离线扣血

**§5.1.5.2 p70 / PDF p71** uses 500 ms checks and `Value = 4*N` for disconnected armor/supercapacitor modules. Both offline loss classes are non-attack under p19. The ledger should not concurrently fabricate projectile damage during an official abnormal-offline state.

**Entity-scope limit:** the p19 non-attack definition says **一方机器人**, not every HP-bearing entity. Table 5-2 p65 gives ordinary collision damage 2 for robot armor, with `-` for base/outpost collision. The p69 offline HP-loss row is **地面机器人**; p70 likewise begins **若地面机器人因自身设计及结构等问题造成装甲模块或超级电容管理模块离线，该机器人将被扣除血量。** No corresponding base/outpost offline HP deduction was located in §5.6.1 p94–96. A source-backed ledger should therefore reject `ArmorCollision` or `ModuleOffline` on `Base`/`Outpost`, rather than silently treating them as supported excluded loss. If the entity schema has only a broad `Robot` kind, upstream must further certify it is an eligible ground robot for these specific mechanisms. A base/outpost physical collision reported as projectile is a different, externally adjudicated category, not evidence of a supported ordinary-collision deduction.

**§7.1.1 p123 / PDF p124** describes automatic referee penalties broadly, including §5.1 mechanisms other than attack/collision and penalty damage. That classification does not override the explicit p19 exclusion. Distinguish **违规判罚扣血** from module-offline automatic deductions.

## 4. Yellow/red warnings and their score attribution

**§5.1.6 p71 / PDF p72** refers yellow/red warning damage to §7.1. **§7.1.2 table 7-2, p124 / PDF p125**; `PEN-02`, `PEN-04`, `AD-10`:

> 裁判系统自动扣除违规机器人当前上限血量的 15%，其余存活机器人被扣除当前上限血量的 5%。机器人每次收到黄牌警告后的 30 秒内，若再次收到黄牌警告，则扣除当前上限血量的百分比是前一次的 2 倍，其余存活机器人被扣除当前上限血量的 5%。
>
> 双方黄牌：所有操作手操作界面被遮挡 2 秒，所有机器人被扣除上限血量的 5%，不计入单台机器人累计黄牌次数。
>
> 若机器人剩余血量小于等于判罚需扣除的血量，则机器人血量降为 1。

Together with p19's explicit opposing-team credit, this supports entering each **actual** yellow-warning deduction for the opposing team. Example: current HP 5, nominal warning loss 30, settled HP 1 gives opposing score +4, not +30 or +5. A bilateral warning can cause independent deductions on both teams and independent opposing credits. UI遮挡 or chassis power-off alone contributes no HP damage.

The 1 HP floor is in the yellow-warning table section. It must not be generalized to red cards. **§7.1.2 p125 / PDF p126**; `PEN-06`, `AD-11`:

> 若在比赛中罚下地面机器人，机器人血量立即变为 0，图传画面断开、多机通信断开、无法复活。

An externally adjudicated in-match ground-robot red card therefore produces an actual loss to zero; p19 assigns that violation deduction to the opponent. The ledger need not decide the warning trigger, history, or penalty validity itself.

Unresolved warning escalation, `PEN-05`, p124:

> 每局比赛中，所有机器人累计收到 3 次黄牌警告时，将同时收到红牌警告。

The subject/scope of “所有机器人累计” remains flagged in the matrix. Do not silently decide team-wide versus per-robot escalation while adding accounting.

**Forfeits are a separate administrative override.** §7.1.2 p125 explicitly sets both teams' damage to zero for pre-match forfeit, with prescribed HP/control/time. A generic “base HP became zero” event must not add a fictitious penalty credit and bypass this override. The current match module correctly rejects nonempty referee rulings rather than pretending its ordinary comparison implements forfeits.

## 5. Dart group damage is not ready for default score attribution

**§5.7.3 p111 / PDF p112**; `dart.damage_score_wording_gap`, `AD-15`:

> 若选择“随机移动目标”、“末端移动目标”后命中基地飞镖检测模块，对方全部存活的地面机器人分别立即扣除各自当前上限血量 20%（随机移动目标）、50%（末端移动目标）的血量（计入对方全队总伤害，但该扣血本身以及导致的机器人战亡不会产生经验）。

The literal text is **计入对方全队总伤害**. It must not be silently rewritten as the firing team's attack damage. The team referent and whether this “全队总伤害” equals §5.8 “全队攻击伤害” are unresolved in the existing matrix. Direct target damage and this secondary group loss are separate effects. The experimental projectile-only adapter does not cover either dart path.

## 6. Timeout order and surviving HP

**§5.8 p114–115 / PDF p115–116**; `victory.conditions`, `AD-12`–`AD-14`:

> 单局比赛的胜负按下列编号的优先顺序逐项判定，直至得出最终结果：
>
> 1. 一方基地被击毁时，基地剩余血量高的一方获胜。
> 2. 一局比赛时间耗尽时，若双方基地均未被击毁，累计控制度高的一方获胜。
> 3. 一局比赛时间耗尽时，若双方基地均未被击毁且累计控制度一致，全队攻击伤害高的一方获胜。
> 4. 一局比赛时间耗尽时，若双方基地均未被击毁且累计控制度一致，全队攻击伤害一致，全队总剩余血量高的一方获胜。
> 5. 若上述条件无法判定胜负，该局比赛视为平局。

Item 4 is on printed p114; item 5 is on p115. The matrix's `SCORE-02` page string citing §5.8 p115 is imprecise for item 4; use p114 together with the p19 definition.

**§2 p19 / PDF p20**, `AD-04`:

> 总剩余血量：每局比赛结束，己方所有存活机器人剩余血量的总值。

**§2 p16 / PDF p17**, `AD-05`:

> 存活：机器人裁判系统主控模块正常连入裁判系统服务器且血量不为 0 的状态。

Therefore:

- With both bases alive at timeout, unequal base HP is irrelevant to the timeout comparison.
- Total remaining HP includes current HP of surviving robots, not base/outpost HP, max HP, or a positive-HP abnormal-offline robot.
- A dismissed ground robot has zero HP and cannot be revived; the module explicitly excludes `dismissed` facts too.
- Upstream must supply every scoring robot exactly once. Do not assume every rendered body in a combination system is independently counted.
- **§6.6 p121** defines the seven-minute match; **§6.7 p121**, `AD-16`, ends the round on time exhaustion or an early win condition. 420000 ms is the module's engineering interface representation, not an official sampling frequency.
- If a required criterion is unknown, return the corresponding unresolved/missing condition. Do not use zero or fall through to HP/draw. A control difference can decide the match without requiring the later damage criterion.

## 7. Simultaneous events and terminal boundary

No universal damage/healing/capture/control-credit/timeout ordering or bilateral base-destruction ordering was located in the relevant pinned manual text. Matrix `MODEL-02` preserves this unresolved dependency. The manual's numbered victory conditions are criterion priority, **not** a universal event-processing order.

**§5.1.1 p65 / PDF p66**, `AD-17`:

> 为确保检测精度、减少误检，部分情况下，裁判系统需等待 42mm 弹丸的发射信息匹配，因此装甲模块在检测到 42mm 弹丸打击后，伤害结算可能会略有延迟。

A physical hit timestamp therefore need not be the official settled-damage timestamp. Damage arriving in the timeout tick requires a documented experimental boundary policy or external adjudication. Do not claim “every hit at the horn counts” or its converse as official.

Safe engineering contract:

1. Validate/settle all facts for one declared boundary on a candidate state.
2. Append the boundary's classified deductions, compute the available control/damage/surviving-HP totals, then evaluate the resolved snapshot.
3. Commit the state and freeze related ledgers atomically on terminal outcome. A failure must roll back all totals/sequence state, not leave partial damage credits.
4. Store the named timing policy, source version, ledger state, and result reason in deterministic replay/hash state.
5. Reject further credits after a terminal seal. Treat externally settled same-time facts as inputs; deterministic processing alone is not proof of official correctness.

The unchanged match module returns `UnresolvedSimultaneousDestruction` for both bases zero unless the caller explicitly marks this boundary resolved. That opt-in is an upstream assertion, not proof that the real referee would accept the simultaneous state.

## 8. Existing source audit and integration limits

This section describes the files as inspected before the new accounting implementation. No existing source was changed by this audit.

### `rules/rmuc2027_v1/match_rules.cpp` and `.hpp`

- `surviving_robot_hp()` (implementation lines 66–81) checks team, nonnegative HP, duplicate entity IDs, kind, normal connection, dismissal, and HP > 0. Reuse it; do not sum every HP-bearing object.
- `evaluate()` (lines 83–109) implements ordered base/control/damage/robot-HP comparison with optional totals. Attack damage is explicitly an externally settled p19 ledger in `TeamTotals`.
- `MatchJudge::update()` (lines 111–122) latches the final snapshot, but does not own or seal an external damage ledger. The caller must make this terminal operation atomic.
- Missing attack damage is required only after tied control. Both bases zero are unresolved by default. Nonempty referee ruling is unsupported.
- Preserve the module's current namespace/version and existing tests unless a separately reviewed change is required. New source-backed accounting can be a separate module that feeds its existing API.

### `src/sim.cpp` and `src/sim.hpp`

- `sim.hpp` lines 10–13 declares all physics/constants engineering assumptions: 60 Hz, 100 HP, `DAMAGE=10`, and original 7200-tick / 120-second duration.
- `sim.cpp` lines 134–150 allows both robots to fire before damage commits. This is an explicit sandbox simultaneity policy.
- Lines 165–177 only hit the one opposing robot, add nominal `DAMAGE` to a per-victim accumulator, and emit kind-2 hit events containing **nominal** `DAMAGE`.
- Lines 188–192 clamp aggregate HP to zero after all bullets; `Event.amount` is not amended to reflect actual loss. A sum of kind-2 amounts can overcount actual loss at death.
- The two-actor engine makes team-level attribution simple only because all damaging bullets to each target have the same opposing owner. It does not solve mixed-source allocation or a multi-robot official damage system.
- There is no friendly fire, penalties, offline deduction, darts, aerial impact, armor validity, shields/buffs, healing, respawn, or base damage in this native sandbox.
- Original line 195 can stop on robot disable, which is not an official match win condition. The tactical adapter's separate `stop_on_disable=false` and 25200-tick / 420-second duration remain an explicit experimental mode, not changes to legacy sandbox defaults.

### `tactical_mode/core.cpp`

The inspected core already labels itself experimental, uses non-destructible base HP=1 sentinels, provides one robot per team, and leaves tied-control timeout unresolved because attack damage is missing. Supplying source-audited **experimental** attack totals and explicitly assumed connected/non-dismissed robot facts may extend this mode to damage/HP/draw resolution. It does not implement official base destruction or validate the sentinel as a real base model.

When adding this extension, update all result reason paths: the inspected `result_json()` currently labels either winner `timeout-control` and has no terminal draw return. A correct new judge result must not be displayed as a control win or as ongoing. Include the damage ledger and sealed/completeness state in replay hashes and update the configuration/provenance label. Keep attack totals out of policy-visible observations unless their disclosure is separately an explicit experimental broadcast choice.

## 9. Suggested acceptance cases

These test source-backed accounting separately from the experimental producer:

- Qualifying opposing projectile deduction 20 credits the attacker +20; equivalent ordinary collision 2 or module-offline 4 credits neither team.
- Low-HP isolated settled projectile loss 5 credits 5, even if nominal hit strength is 20. Do not claim this tests unresolved official per-hit overkill allocation.
- Two same-victim synthetic opposing hits with nominal 20 total and pre-HP 7 produce one aggregate loss 7; both hit events may remain nominal, but credited total is 7.
- Yellow warning pre-HP 5 to HP 1 credits opponent +4; red-card pre-HP 5 to 0 credits +5. An administrative pre-match forfeit is rejected or handled as its own override, not generic damage.
- Friendly/unknown-source or mixed-cause attribution without external resolution leaves attack total unavailable; dart secondary group loss remains unresolved.
- Simultaneous opposing lethal hits can credit each side's separately settled deduction, without asserting an official bilateral base outcome or individual kill ownership.
- At timeout: higher control wins even with less damage/HP; tied control uses damage; tied control/damage uses surviving current robot HP; all resolved equal yields draw.
- Larger living base/outpost HP never wins an otherwise tied timeout. Positive-HP abnormal-offline/dismissed facts do not increase surviving robot HP.
- Required missing damage does not become zero, does not fall through to HP, and does not become draw. Disabled experimental credits preserve their own unresolved control path.
- Base/outpost ordinary-collision or module-offline events are rejected as unsupported entity/cause pairs. A broad Robot entity fact still requires upstream ground-robot eligibility where the source specifies it.
- Duplicate/gapped/replayed credits, negative loss, overflow, impossible HP transitions, post-seal credits, or unsupported mixed causes must not partially mutate accounting.
- Repeat replay produces identical ledger totals/result/hash. Once terminal, no new projectile/card/credit can change the frozen result.

## Audit completion

The source PDF checksum, relevant printed/PDF page mapping, five rendered page layouts, and 17 machine-readable source quotes were verified. This work supplies a bounded accounting contract and identifies its unresolved upstream dependencies. It does not certify the full simulator, the latest official rule amendments, or real referee timing.
