# 车辆配置与车库（`rmuc-vehicle.v1`）

每辆车是一份 JSON 文档。浏览器打开 `http://127.0.0.1:8765/garage.html` 可以编辑、保存、导入、导出；
也可以直接手写 JSON 后导入。保存的车辆放在本机 `user_data/vehicles/`（不进版本库，重启服务后仍在；
用 `python3 server.py --data-dir 目录` 或环境变量 `RMUC_DATA_DIR` 可以换位置），
在 `field.html` 的“对局设置”里按出场位置选用。

## 哪些能配，哪些不能

| 部分 | 内容 | 性质 |
|---|---|---|
| `class`、`rules` | 兵种；步兵的底盘类型 / 发射机构类型；哨兵自动 / 半自动 | **规则选择**。上限血量、底盘功率上限、热量上限与冷却、初速度上限、初始允许发弹量由 [`growth_v1`](../rules/rmuc2027_growth_v1/README.md) 按手册表 3-3~3-6、5-16~5-18 给出，**不能直接填写** |
| `chassis` | 驱动形式、尺寸、质量、速度、加速度、越障高度、坡度、功率模型、超级电容 | 工程参数，按自己的车填写 |
| `gimbal` | 限位、角速度、角加速度、增益、安装位置 | 工程参数 |
| `launcher` | 口径、设定初速度及波动、射频、散布、热量保护 | 工程参数（口径必须与兵种相符） |
| `armor` | 每个侧面至多一块装甲：大小、高度、偏移 | 工程参数 |
| `sensors` | 相机视场 / 距离 / 帧率 / 延迟 / 噪声，激光雷达 | 工程参数 |

参数的键名、单位、取值范围和默认值只在一处定义：[`vehicle.cpp`](vehicle.cpp) 里的参数表。
页面表单、校验和默认值都由它生成（`GET /api/field/vehicles/schema`）。
范围只是防止明显不合理的输入，**不是**官方制作规范的尺寸 / 重量限制（那份文件不在本仓库的资料里）。

除范围外还检查：轴距不超过车长、轮距不超过车宽；碰撞半径不小于车体短边的一半、不超过半对角线 + 50 mm；
横移速度不超过前进速度；云台俯仰轴与激光雷达不高于整车高度；装甲不超出所在侧面、不高于整车高度；
步兵 / 哨兵为 17mm 发射机构，重装为 42mm 或不安装。缺少的键取默认值，未知的键被拒绝，`format` 与 `name` 必填。

发射机构、装甲和传感器参数现在已经进入配置、校验和保存；它们在[开发计划](../docs/PLAN.md)第 3 项
（弹道、命中判定、感知接口）接入引擎后生效。底盘和云台参数已经生效。

## 内置默认车辆

位于 [`vehicles/`](vehicles/)，只读；要改请在车库里“另存为新车辆”。数值是工程默认值，不是任何实车的实测数据。

| ID | 名称 | 特点 | 规则属性（1 级） |
|---|---|---|---|
| `builtin:infantry_mecanum` | 麦轮步兵（默认） | 麦轮、带超级电容 | 200 血 · 45 W · 热量 40 / 冷却 12 |
| `builtin:infantry_omni` | 全向轮步兵 | 横移与前进一样快；功率优先 + 爆发优先 | 150 血 · 60 W · 热量 170 / 冷却 5 |
| `builtin:infantry_balance` | 平衡步兵 | 不能横移，直线 4.2 m/s，前后两块大装甲 | 150 血 · 60 W · 热量 40 / 冷却 12 |
| `builtin:infantry_climber` | 越障步兵 | 越障高度 220 mm，可直接登上 200 mm 台阶 | 200 血 · 45 W |
| `builtin:infantry_low` | 低矮步兵（可穿隧道） | 整车高 240 mm，可穿过净高 250 mm 的隧道 | 200 血 · 45 W |
| `builtin:heavy_default` | 重装机器人（42mm） | 32 kg，已装 42mm 发射机构，四块大装甲 | 600 血 · 50 W · 热量 300 / 冷却 100 |
| `builtin:sentry_auto` | 自动哨兵 | 全向底盘，360° 激光雷达，广角相机 | 400 血 · 100 W · 热量 260 / 冷却 30 |

## 文档示例

```json
{
 "format": "rmuc-vehicle.v1",
 "name": "我的步兵",
 "notes": "",
 "class": "infantry",
 "rules": {"chassis_type": "power_first", "launcher_type": "cooling_first", "sentry_mode": "auto"},
 "chassis": {"drive": "mecanum", "length_mm": 550, "width_mm": 470, "height_mm": 480, "mass_kg": 18,
             "max_speed_mps": 3.5, "max_step_mm": 30, "supercap_energy_j": 2000},
 "armor": [{"face": "front", "size": "small", "center_height_mm": 150, "offset_mm": 0}]
}
```

省略的参数取默认值；保存时写入的是补全后的完整文档。完整键表见车库页面或 `vehicles/` 里的内置文件。

## 接口

| 接口 | 说明 |
|---|---|
| `GET /api/field/vehicles` | 车辆列表：ID、名称、兵种、内容哈希、1 级规则属性；失效的存档会标出原因 |
| `GET /api/field/vehicles/schema` | 参数表（键、单位、范围、默认值、选项）与中文标签 |
| `POST /api/field/vehicles/get` | `{"id": …}` → 完整文档与各等级规则属性 |
| `POST /api/field/vehicles/check` | `{"vehicle": …}` → 校验并补全，不保存 |
| `POST /api/field/vehicles/save` | `{"vehicle": …, "id": null}` 新建；`"id": "garage:…"` 覆盖。内置车辆只读，名称不能与其它车辆重复 |
| `POST /api/field/vehicles/delete` | `{"id": "garage:…"}` |
| `POST /api/field/reset` | `{"robots":[{"id":"R1","team":"red","vehicle":"garage:…"}]}`；不写 `vehicle` 用默认车 |

对局信息（`/api/field/meta` 的 `session`）记录每台机器人所用车辆的 ID、名称和内容哈希（规范化 JSON 的 SHA-256），
所以事后可以确认一局用的是哪一版配置。校验只由 C++ 引擎完成，Python 侧负责存储。
存档文件损坏或在参数表变化后不再合法时，不会被加载，只在列表里标为失效，可以删除。
