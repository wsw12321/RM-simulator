# RMUC 2027 · 手工已裁定事实场景工具

**MANUALLY SUPPLIED ADJUDICATED FACTS ONLY. 非官方、非完整比赛模拟器。**

这是与浏览器沙盒完全分离的场景 CLI：可审阅的 JSON 输入 → 原有 C++
`rmuc2027_v1` 结算模块 → 带版本/出处的确定性事件日志。**没有修改规则模块语义，
也没有改变浏览器的 100 HP、演示地图、120 秒终局或 sandbox-v1 回放。**

它只回答：“如果这些人工提供、已经裁定的事实和顺序成立，这个公式模块会怎样结算？”
输入声明不证明事实真的经过裁定，也不证明数值/事件在真实比赛中能够发生。

## 构建与运行

依赖与原项目相同：C++20、Python 3.10+、Bash。无额外包，无联网服务。

在项目根目录运行：

```bash
bash tools/build.sh
python3 scenario_harness/cli.py run scenario_harness/scenarios/timeout_control_priority.json --output build/timeout-control.log.json
python3 scenario_harness/cli.py verify build/timeout-control.log.json
python3 scenario_harness/cli.py extract build/timeout-control.log.json --output build/timeout-control.roundtrip.json
python3 scenario_harness/cli.py run build/timeout-control.roundtrip.json
```

输出路径必须尚不存在；不会覆盖已有文件。省略 `--output` 则将完整日志写到 stdout。
从其他目录调用 CLI 也可运行；输入/输出路径相对于调用者目录，程序使用自己的源码目录。

退出码：

- `run`: `0` 表示所有边界被接受（可能仍未终局）；`1` 表示日志生成成功，但有拒绝的边界；
  `2` 表示 JSON/模式/资源/构建/执行/输出错误，stderr 给出 JSON 诊断。
- `verify` / `extract`: `0` 表示完整重算一致，`2` 表示失败。原日志包含预期的拒绝事件，
  不妨碍验证通过。`extract` 先完整验证，再导出原场景。
- 格式错误在执行前拒绝，不生成部分日志。合法输入中的规则错误会记录并继续处理后续尝试。
- 场景上限 8 MiB、日志上限 32 MiB。Unicode 转义/缩进可能使输出变大；写入前按实际输出
  字节检查限制。超限返回 `OutputResourceLimit`，不生成部分文件，也不输出部分 stdout。

例如，包含预期拒绝的终局测试：

```bash
python3 scenario_harness/cli.py run scenario_harness/scenarios/terminal_immutability.json --output build/terminal.log.json
# 此处退出码 1 是预期的：后两个尝试得到 AlreadyFinished。
python3 scenario_harness/cli.py verify build/terminal.log.json
```

## 如何改场景

先复制一个 `scenarios/*.json`。完整字段和严格范围见 [FORMAT.md](FORMAT.md)。

- 所有事实都由你提供，包括精确毫秒边界、基地 HP、控制分、攻击伤害、存活机器人 HP
  总和以及控制权。未确定的比较项用 `null`，不当作 0，不根据上一帧/HP 差推测。
- `external_totals` 使用人工给定控制分，禁止任何计分 credit。
- `manual_one_second_credits` 使用手工供应的完整一秒 credit 历史，账本从 0 开始；
  禁止同时填写 `control_score`，即使填写相同数字或 null 也会拒绝。每个 credit 提供
  四处阵地的已裁定控制权/程度。程序只应用 1/3 点公式，不决定何时产生 credit。
- 边界序号从 1 连续递增；所有尝试的时间非递减。相同时间按列表顺序执行。每个边界
  **先计入明确列出的 credit，再判断完整快照**，成功后整体提交，失败整体回滚。
  这是显式的适配器输入约定，不是手册给出的同刻优先级。
- 终局提交与账本封存是同一事务。后续尝试仍进入审计日志，但不能改变结算或继续加分。
- `source_note` 写明事实来源或合成案例假设。提供的例子全部是合成测试，不是真实赛事记录。

## 随附案例

- `timeout_control_priority.json`: 419999 ms 尚未结束；420000 ms 蓝方控制度获胜，
  即使红方基地 HP、攻击伤害和存活 HP 都更多。
- `timeout_attack_priority.json`: 控制度相同，先比较攻击伤害，红方胜。
- `timeout_surviving_hp.json`: 控制度/伤害相同，由存活机器人当前 HP 总和判蓝方胜。
- `timeout_draw.json`: 三项比较全同，基地未毁且 HP 不等仍平局。
- `early_base_ending.json`: 12001 ms 蓝基地为零，红方提前胜；较低顺位未提供不妨碍结算。
- `missing_inputs.json`: 缺少控制、攻击伤害、存活 HP 依次明确报错；同一时间的最后一份
  完整快照才得到平局。每次失败不修改判断器时间或状态。
- `unsupported_ruling.json`: 两次裁判路径均明确拒绝，候选 credit 也回滚；最后无判罚输入
  的 credit 只计入一次。这不表示可通过撤掉真实判罚来替代正式裁判。
- `terminal_immutability.json`: 手工计分 `[4,0]`，下一边界计分后为 `[5,3]` 并因蓝基地为零
  终局；同刻及更晚的更改均被拒绝，账本与终局快照不变。
- `unresolved_simultaneous_bases.json`: 双基地同为零先明确报未解决；仅在后续输入明确声明
  上游已解决同时毁坏时按原模块返回平局，不代表真实裁判必然接受。

## 日志、出处与验证

日志嵌入全部输入与 source_note、逐边界成功/错误、规则 ID、候选 credit、已提交前后状态、
终局快照、模式/日志/协议/模块版本、模块 provenance 全文和源码 SHA-256。

该 provenance 源于已审计矩阵 `r2-audit-complete`，矩阵 SHA-256 为
`f652d2f7c5fd9e88af660ad8fc46baa7ce8b36e4480dd6f18c43860466cecdea`。
原手册、页码、链接和未解决范围随日志保留；无需在另一台机器找到研究用矩阵的绝对路径。
这没有升级任何“未解决”条目，也没有完成最新官方答疑核验。

启动原生执行前比较编译进驱动的源码身份与当前文件，再检查执行返回身份；源码改动后未重建
会报 `BuildIdentityMismatch`。保留对应源码并重新构建，才能重放旧日志。日志不记录墙钟、
绝对路径或随机值，重复运行、提取再运行的字节输出一致。SHA-256 只是漂移检测，既不是
裁判签名，也不是二进制/编译器认证；不能阻止攻击者同时修改数据和哈希。

```bash
bash tools/check.sh
# 或构建后只运行场景工具测试：
python3 tests/test_scenario_harness.py
```

测试覆盖输入拒绝、显式顺序、优先级、事务回滚、终局封存、错误重放、构建失配、日志篡改
及 CLI 提取/重跑。具体实际执行结果见 `../docs/VERIFICATION.md`。

## 没有实现，也不能由这些日志推断

- 5 秒占领、2 秒失效宽限、场地接触、控制权转换或任何 capture scheduler。
- 首个计分时刻、秒边界采样、亚秒余量、真实裁判同刻顺序。**credit 的数量/时机没有与
  elapsed_ms 做因果校验**；人工提供错误历史会得到错误前提下的可重复结果。
- 从 HP 减少推断攻击伤害、专项飞镖归属、判罚扣血计算或裁判判负/改判/重赛。
- 机器人实体生存资格汇总：此版直接消费已经裁定的总和，原模块的实体汇总 API 仍独立存在。
- 正式 7 分钟对局物理仿真、全部机器人/武器/经济/复活、BO 系列赛或官方合规证明。
- 浏览器接入、WASM、真实 Chromium 交互 QA、跨平台一致性。
