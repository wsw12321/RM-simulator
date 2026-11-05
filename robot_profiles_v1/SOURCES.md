# RMUC 2027 V1.0.0 sourced parameter projections

Audit date: 2026-11-05 UTC. Companion evidence and candidate records: `source_audit.json`, audit revision `1.0.0`.

## Result and strict boundary

These are **sourced parameter presets for an experimental simulator, not complete official robot classes**. The audited catalog boundary is:

- **40 infantry snapshots:** two chassis choices, two 17 mm launcher choices, and levels 1-10. Only `max_hp`, `heat_limit_tenths`, and `cooling_per_second` are source-backed.
- **4 heavy HP-only snapshots:** opening, first assembly, second assembly, and third assembly **or** successful modular 42 mm launcher installation. Only `max_hp` is source-backed.
- **2 sentry HP-only snapshots:** automatic and semi-automatic control mode. Only `max_hp` is source-backed. The selection does not implement either official control mode.
- **No aerial, hangar, dart, or radar projection. No legacy Hero/Engineer aliases.** No applicable ground-actor HP/17 mm heat projection was established for these systems.

Only fields explicitly present in a selected candidate's `overrides` are sourced. Other inherited native configuration values and all unimplemented behaviors remain experimental. HP-only actors can still behave like synthetic 17 mm ground actors in the engine; they must never be presented as an implementation of heavy or sentry role mechanics.

The source itself changed the role taxonomy. **Printed p13 / PDF p14, §1.2** says:

> 原英雄机器人和工程机器人合并为重装机器人；
>
> 原空中机器人调整为空中系统（包含空中机器人和机库）
>
> 取消英雄机器人特殊机制、工程机器人特殊机制、空中机器人特殊机制。

Table 3-1, printed p20, lists heavy, infantry, sentry, aerial system, dart system, and radar system. This audit does not import a previous season's Hero/Engineer/Aerial attribute tables or infer role eligibility from simulator actor labels.

## Source and verification

- **Document:** RoboMaster 2027 机甲大师超级对抗赛比赛规则手册 V1.0.0（20261015）.
- [Official source hub](https://bbs.robomaster.com/wiki/20204847/1969149?source=7).
- [Pinned official PDF](https://bbs-web-static.robomaster.com/451e5737ba5d4463909a3013456a4e551792055647969/RoboMaster%202027%20%E6%9C%BA%E7%94%B2%E5%A4%A7%E5%B8%88%E8%B6%85%E7%BA%A7%E5%AF%B9%E6%8A%97%E8%B5%9B%E6%AF%94%E8%B5%9B%E8%A7%84%E5%88%99%E6%89%8B%E5%86%8CV1.0.0%EF%BC%8820261015%EF%BC%89.pdf).
- Independently recomputed PDF SHA-256: `9e2eed9295536c6fd547b62d634df1d5e70a2e1e2250e5dff3521d557e5d54be`.
- Source size: 22,455,861 bytes; 144 physical PDF pages.
- **All page numbers below are printed pages. Physical PDF page = printed page + 1.**
- Poppler-rendered pages independently inspected in pixels: 13, 15, 17, 20, 22, 24-28, 64-67, 89, 91-94, 105, 108-110, 112. Render maximum dimension: 1500 pixels.
- All 20 infantry HP rows, 20 infantry heat rows, four heavy rows, and two sentry mode blocks were checked visually. Particular checks: the p92/p93 HP-priority continuation; power-priority level10 HP400; cooling-priority level10 Q0=120; heavy600 OR condition; and sentry posture fractions.
- `source_audit.json` contains 73 reviewed statements and 155 exact quote fragments. Every fragment was checked against the source extract after whitespace normalization. Where table row labels or fractions interleave with nearby columns, the JSON names the layout issue and records the pixel interpretation separately.
- The derived rules matrix was not treated as authority. No network access was needed for this audit. Latest official Q&A, construction manual, communication protocol, and referee firmware were not newly verified.

This is a pinned-manual claim, not a latest-rules certification. Printed p15 explicitly says official Q&A and rule files have equal effect, with the latest publication controlling differences. The source hash must accompany the projection version rather than being replaced by the native simulator's different source pin.

## 1. What each engine field can mean

| Native field | Audit decision | Reason |
| --- | --- | --- |
| `max_hp` | Selected rows supported | Direct upper-HP table value under the specified role, level/chassis, phase, or mode. |
| `heat_limit_tenths` | Static unbuffed infantry supported | Table 5-18 Q0 multiplied by 10 solely to represent heat in tenths. |
| `cooling_per_second` | Static unbuffed infantry supported | Table 5-18 heat units per second, unchanged. |
| `ammo` | Excluded | Initial native inventory is not referee firing allowance. |
| `radius_mm` | Excluded | No supported circular collision-radius value. Construction dimensions would not by themselves define collision geometry. |
| `speed_mm_tick` | Excluded | Chassis power limits in watts are not translation speeds. |
| `projectile_speed_mm_tick` | Excluded | Muzzle speed limits are not constant actual projectile speeds. |
| `projectile_damage` | Excluded | Damage depends on valid armor detection, attack type, and buffs. A synthetic circle hit is not an official armor-module event. |
| `cooldown_ticks` | Excluded | Armor detection intervals and posture/gate cooldowns do not specify launcher firing cadence. |
| `sensor_range_mm` | Excluded | No supported constant sensor-range mapping. Radar error thresholds describe accuracy, not range. |

### HP is not automatic full healing

Printed p17, §2 table 2-1 (`RP-HP-DEFINITIONS`), distinguishes:

> 初始血量: 比赛开始时，裁判系统为机器人设定的血量值。
>
> 当前血量: 机器人的实时血量值。
>
> 上限血量: 机器人血量可以恢复到的最高值。

The mapped value is **upper HP**, not independently sourced current HP. If the native engine initializes current HP to the chosen maximum, that is an experimental fixture initialization, especially for infantry levels2-10 or a later heavy phase. It does not reconstruct an official mid-match state. Likewise, initial heat0 is a fixture choice, not a conclusion drawn from an attribute row.

Printed p91, §5.5.2, says:

> 机器人升级后，血量上限提升的部分将被立即添加至机器人的当前血量。

This is addition of the cap increase, not a full heal. The preset layer must not add an upgrade or healing mechanic. Sentry table3-6 explicitly pairs initial and upper HP, but a later arbitrary sentry fixture spawn at full HP is still not an official revival or phase transition.

## 2. Infantry: level, chassis, and launcher are required

Printed p89, §5.5.1 (`RP-INFANTRY-LEVEL`):

> 比赛开始时，步兵机器人的等级为 1 级，可通过增加经验值实现等级提升，其余机器人均不适用经验体系。

Printed p91, §5.5.2 (`RP-INFANTRY-SELECTION`), permits independent chassis and launcher selection, fixes the choices after the stated start/selection condition, and defaults to **HP-priority chassis** and **cooling-priority launcher** if not selected. This justifies the Cartesian product of two chassis and two launcher choices at a common level. It does not justify mixing HP at one level with heat at another, changing choices dynamically, or declaring level10 an official opening state.

### Verified numeric rows

HP columns: table5-17, pp92-93 / PDF pp93-94. Heat columns: table5-18, pp93-94 / PDF pp94-95. Cooling is in heat units per second. Q0 is in heat units.

| Level | Power-priority max HP | HP-priority max HP | Burst Q0 | Burst C/s | Cooling Q0 | Cooling C/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 150 | 200 | 170 | 5 | 40 | 12 |
| 2 | 175 | 225 | 180 | 7 | 48 | 14 |
| 3 | 200 | 250 | 190 | 9 | 56 | 16 |
| 4 | 225 | 275 | 200 | 11 | 64 | 18 |
| 5 | 250 | 300 | 210 | 12 | 72 | 20 |
| 6 | 275 | 325 | 220 | 13 | 80 | 22 |
| 7 | 300 | 350 | 230 | 14 | 88 | 24 |
| 8 | 325 | 375 | 240 | 16 | 96 | 26 |
| 9 | 350 | 400 | 250 | 18 | 104 | 28 |
| 10 | 400 | 400 | 260 | 20 | 120 | 30 |

The final row must be copied, not extrapolated: power-priority HP jumps to400, HP-priority HP remains400, and cooling-priority Q0 jumps to120 rather than112. Table5-17 power limits are retained in evidence for correct column identification, but not mapped to simulator movement speed.

Each JSON record has a stable ID of the form `infantry_{power|hp}_{burst|cooling}_l{1..10}`, a strict scalar-valued `conditions` object, the three `overrides`, and separate provenance for each field. Every provenance record resolves to an exact table-row statement, source hash, printed/PDF page, source value, units, and transform.

The candidate conditions require:

- Exact source role, chassis choice, launcher choice, and common level.
- A static snapshot, no external buffs, and no simulated progression.

These are scope declarations for the projection. They do not claim that the synthetic actor's physical construction, control policy, team slot, ammunition, or history satisfies official eligibility rules.

### Unit mapping and heat dependencies

- `max_hp = official upper HP`, integer HP, identity transform.
- `heat_limit_tenths = Q0 * 10`, exact integer representation of a threshold.
- `cooling_per_second = C`, identity transform in heat units/s.

Printed p66, §5.1.3, says every detected17mm launch adds10 heat and cooling settles at10Hz by C/10 per cycle. In tenths, one100ms cooling cycle removes C heat-tenths. Example: cooling-priority level1 has Q0=40, so the stored threshold is400; C=12 remains12/s, giving1.2 heat per100ms, or12 stored tenths. The catalog does not change the native scheduler, gate ordering, or initial cooling phase.

Q0 is not a hard cap on stored heat. The existing heat audit documents lock behavior and the unresolved exact higher threshold: p66 prose uses `Q1>Q2`, while the p67 diagram uses `Q1≥Q2`. A parameter catalog does not resolve that conflict. Preserve the explicitly named experimental heat policy and provenance. See `../tactical_mode/HEAT_SOURCES.md` and its companion JSON for lock, death, buff, and same-time ordering dependencies.

The cooling numbers are **base values**. Printed p105 says the greatest same-kind buff is selected, including heat cooling. This catalog does not apply rune, field, fortress, or posture buffs. Changing a cooling modifier or level during a run requires additional rules and provenance, not silent mutation of a static preset.

## 3. Heavy: only HP snapshots

Table5-16, pp91-92 / PDF pp92-93:

| Selected source row | max HP | Other table values, excluded |
| --- | ---: | --- |
| 开局默认 | 400 | Power limit50; launcher values are dashes |
| 完成一级装配后 | 450 | Power limit50; launcher values are dashes |
| 完成二级装配后 | 500 | Power limit50; launcher values are dashes |
| 完成三级装配后或成功安装模块化42mm发射机构后 | 600 | Power limit50, 42mm Q0=300, cooling100, muzzle speed limit16.5m/s |

The last condition is **third assembly completed OR successful modular42mm installation**. Do not require both. Do not choose an earlier HP row for a robot that is already in the600-HP state. This is selection of a table row, not an implementation of phase attainment, precedence for simultaneous events, current-HP transitions, or launcher permissions.

Printed p22 table3-3 says:

> 1 个可安装的模块化 42mm 发射机构
>
> 成功安装模块化 42mm 发射机构后为 16.5

Printed p108, §5.7.1, adds material dependencies: early defense before installation; alternative launcher unlock paths; installation at the workbench; and a detected confirmation shot after which installation is considered successful. That confirmation42mm shot does not count heat, muzzle speed, or damage. None exists in the native17mm engine.

Consequently heavy Q0=300 and C=100 are **not** applied to this17mm heat adapter. Dashes in earlier rows are not numeric zero. The four candidate IDs end in `_hp_only`, and every firing/heat field remains expressly unsourced. No legacy hero or engineer alias is provided.

## 4. Sentry: control mode and posture prevent a naive heat import

Printed p26 / PDF p27, table3-6:

| Control mode | Initial/max HP | Base Q0 | Base cooling/s | Chassis power limit |
| --- | ---: | ---: | ---: | ---: |
| Automatic | 400 | 260 | 30 | 100W |
| Semi-automatic | 200 | 100 | 10 | 60W |

Printed p108 allows the commander to select automatic or semi-automatic mode before self-check; automatic is the default. These are official operating modes, not labels for whether the simulator happens to use a bot.

Printed p109 / PDF p110, §5.7.2 table5-28, says:

> 哨兵机器人开局默认处于移动姿态。
>
> 当前热量始终为 0，发射弹丸不增加热量
>
> 上述计算均取整数值。
>
> 当效果为热量冷却与底盘功率下降时，其在哨兵机器人已完成热量冷却增益和底盘功率增益计算的值的基础上乘算。

The second quotation belongs specifically to **enhanced attack posture**. Movement and defense reduce the selected effective cooling rate to1/3. Ordinary attack grants3x cooling, degraded attack2x. Posture switching has a5s cooldown, degradation occurs after cumulative time in a posture **exceeds**3 minutes, and enhanced modes have independent15s allowances. These numbers are not firing cooldowns.

Thus an unbuffed automatic sentry starting in movement would cool at30/3=10/s, not30/s. Semi-automatic movement gives10/3 before the unspecified integer-taking direction; this audit does not choose floor, nearest, or ceiling. Enhanced attack cannot be modeled merely by choosing a very large cooling number. The p109-110 example selects105/s after buff comparison, then multiplies1/3 to obtain35/s, confirming the ordering.

The two candidates therefore import **only HP400 or HP200**. They intentionally exclude even the known baseQ0 to avoid presenting a partly qualified heat profile as complete. Their conditions say posture and role mechanics are not simulated, and the labels explicitly say HP-only.

## 5. Other candidate systems and excluded mappings

### Aerial robot, hangar, and aerial system

Printed p27, tables3-7 and3-8, treats the aerial robot and hangar separately. The aerial robot starts at the hangar, operates automatically with an emergency-stop remote, and communicates through the hangar. The **hangar** table says launcher not applicable and ammunition loading/supply not allowed. Those hangar entries must not be misattributed to the aerial robot.

No applicable ground max-HP or17mm heat value is established by these attribute tables. A planar ground actor does not implement flight height, aerial collision/impact, launch/recovery, or hangar behavior. The2027 aerial impact detection table on p64 also contains pending quantities. No aerial preset or invented flight height/range is provided.

### Dart system

Printed p27 table3-9 and p110 §5.7.3 describe a dart system, including four loadable darts per round and gate/detection windows. This is not native17mm ammunition or a17mm firing cooldown. There is no supported ground HP/17mm heat projection.

### Radar system

Printed p28 table3-10 describes information acquisition and communication. Printed p112 §5.7.4 defines location-error accuracy thresholds0.8m and1.6m, marking progress, and communication-dependent updates. Those thresholds are not sensor range. No radar HP,17mm heat, or native sensing-radius projection is supplied.

### Ammunition and firing allowance

Printed p17 defines allowed firing quantity as the number currently permitted to be fired, and says an unlocked launcher's power also depends on that allowance. Tables3-3/3-5/3-6 permit physical preloading while specifying initial allowances0 for heavy/infantry and300 for sentry. Therefore0/300 must not be copied into a field whose meaning is initial physical inventory. No source claim is made for native `ammo`.

### Damage, muzzle speed, and firing interval

Tables3-5/3-6 give a **limit** of25m/s; table3-3 gives a conditional heavy limit16.5m/s. No constant actual speed follows from those limits, and converting them to native per-tick velocity would not fix the semantic mismatch.

Printed pp64-65, §5.1.1, requires valid armor detection and uses the impact normal speed component, not merely muzzle speed. The17mm threshold is greater than12m/s; minimum armor detection interval is50ms. Table5-2 gives raw20HP17mm damage to robot armor **without buffs**. None justifies20HP on every synthetic circle hit or a3-tick firing cooldown at60Hz. The damage table is target/type-specific; attack/defense/vulnerability and hit-detection behavior are absent from the preset adapter.

## 6. Catalog integrity and acceptance criteria

The companion data is intentionally auditable rather than a declaration that the whole game is implemented:

1. Every candidate has `status: verified`, `official: false`, an explicit partial-projection kind and label, exact scalar conditions, field provenance, excluded fields, unsupported behaviors, and an initialization caveat.
2. Only the three-field infantry records and one-field heavy/sentry records may be materialized. No inferred constants may be added under official provenance.
3. Field provenance resolves to the correct row and source document. Heat source values remain Q0 and C; only the storage threshold is multiplied by10.
4. Unknown levels, chassis/launcher choices, role aliases, modified conditions, or unsupported role requests must not silently fall back to a different sourced preset.
5. No source row implies official initial ammunition, physical dimensions, actual speed, universal damage, firing cadence, sensing range, role eligibility, official team composition, or complete behavior.
6. No source max-HP row implies full healing on upgrade or arbitrary phase selection. Native fixture initialization remains labeled experimental.
7. Native roster source pins, native config grammar, and raw replay behavior remain unchanged. Provenance belongs in a versioned outer wrapper around resolved native numeric input.
8. A bare exported native config loses the catalog conditions and source linkage. It must not be relabeled as official or fully sourced merely because some numbers originated here.
9. Hashes bind the exact source-audit and catalog revisions. Later manual/Q&A changes require a newly reviewed version, not a silent update under the same pin.

This audit authored only `SOURCES.md` and `source_audit.json` in this new module directory. It did not change the native engine, roster defaults, heat implementation, or replay formats. Implementation tests and independent review belong to their respective module artifacts; the source checks above are not a substitute for those tests.
