# RMUC 2027 兵种性能与经验等级模块 v1

`rmuc2027-growth-v1.0.0-module.1`。纯函数，没有时钟、物理或裁判状态。依据：
[`docs/rules/`](../../docs/rules/README.md) 中固定的 2027 中文 V1.0.0 手册（SHA-256 `9e2eed92…54be`），页码为印刷页码。

## 已实现（逐项对应手册）

| 内容 | 出处 | 接口 |
|---|---|---|
| 步兵底盘（功率优先 / 血量优先）1–10 级上限血量、底盘功率 | §5.5.2 表 5-17，p92–93 | `performance` |
| 步兵 17mm 发射机构（爆发优先 / 冷却优先）热量上限、冷却 | §5.5.2 表 5-18，p93–94 | `performance` |
| 未选择时默认血量优先 + 冷却优先 | §5.5.2，p91 | `DEFAULT_CHASSIS/LAUNCHER` |
| 重装开局 / 一级 / 二级装配 / 三级装配或装上 42mm：400/450/500/600 HP，50 W，装上后 300 热量、100/s、16.5 m/s | 表 5-16，p91–92 | `performance` |
| 自动哨兵 400 HP / 260 / 30/s / 100 W；半自动 200 / 100 / 10/s / 60 W | 表 3-6，p26 | `performance` |
| 初始允许发弹量：哨兵 300，其他 0 | 表 3-3/3-5/3-6 | `initial_allowance` |
| 17mm 初速上限 25 m/s | 表 3-5/3-6 | `muzzle_speed_limit_mm_s` |
| 等级所需总经验 0/550/…/4400/5000 | 表 5-15，p91 | `level_for_experience_tenths` |
| 只有步兵适用经验体系；击毁计算中哨兵视为 1 级 | §5.5.1，p89 | `uses_experience` / `kill_level` |
| 全队等级上限：二级装配前 5，二级后 7，三级后 10；重装装配后升至 5/7/10 级 | 表 5-12，p83 | `team_level_cap` / `heavy_level` |
| 装配首次完成收益（金币流、防御增益、42mm 解锁）与重复完成的额外金币 | 表 5-12，p83 | `first_assembly_reward` / `repeat_gold_per_10s` |
| 发弹 1 经验；对机器人每点伤害 4、前哨站 2、基地每 2 点 1（奇数向上取整） | 表 5-14，p89 | `shot/damage_experience_tenths` |
| 击毁经验 `50×Lv×(1+0.2×等级差)`，被击毁者等级低时等级差视为 0 | 表 5-14，p89–90 | `kill_experience_tenths` |
| 无来源时平均分配，四舍五入到 0.1；平均经验对应的等级作为击毁者等级 | 表 5-14，p89–90 | `split_tenths` / `unattributed_kill_share_tenths` |
| 首次获得地形跨越 / 制高点增益 300 经验 | 表 5-14，p90 | `TERRAIN_FIRST_CROSSING_TENTHS` |
| 升级后上限血量提升部分立即加到当前血量 | §5.5.2，p91 | `hp_after_max_change` |

经验以 **0.1 为单位的整数**保存，因为平均分配要求保留一位小数；全部运算为整数，不用浮点。

手册的两个例子作为测试用例逐字复算：2 级步兵击毁 6 级步兵得 540；无击毁者时平均经验 2625（5 级）的两台步兵各得 405。

## 明确的工程选择 / 未决边界

- **无来源的非弹丸击毁**：手册只写“平均分给另一方所有存活步兵”，没写此时击毁者等级如何取。默认 `StrictRejectUnadjudicated` 拒绝；`EngineeringAverageLevelForNonProjectileV1` 沿用弹丸情形的平均等级规则。
- 平均经验对应等级也受全队等级上限约束（工程解释）。
- 基地经验的“奇数向上取整”按每次结算的伤害事件计算。
- 等级下降不会出现（经验不减少）；`hp_after_max_change` 对较低上限只做截断，供配置变更使用。

## 未实现

装配过程本身（科技核心位姿、难度解锁时间）、哨兵姿态、能量机关、飞镖 / 雷达经验收益。

## 校验

- `tests/growth_rule_tests.cpp`：全部表行、每级经验边界、等级上限、装配收益、手册例题。
- `tests/test_growth_catalog_crosscheck.py`：与独立审计的 `robot_profiles_v1/catalog.json` 46 个预设逐项比对。
