# 功能验证、迭代证据与当前缺口

记录更新至 2026-10-16。源码完成、电脑测试、目标构建、写入读回和实物结果分别标注；没有视频或测量支撑的结果不升级为完整指标验收。

## 上层车遥控行驶与起停

**2026-10-02：**保留四个可追溯固件检查点与 HEX，见 [上层迭代记录](01-upper-car.md)。最新 S 曲线固件 11220 字节，既有记录确认写入及独立读回一致；起停效果、电流和温升仍需实测。

**截至 2026-10-16：**队员确认原车能行驶、横移通过 150mm 桥面。已有行驶视频没有覆盖完整过桥，且 720×1280、约23fps 不符合本次中期视频要求，需要另行拍摄合格材料。

**2026-10-16 软件复验：**`python sample-mecanum-car-study/tests/run_tests.py` 全部通过。覆盖九个应用 C 文件与真实 STM32 头文件、Keil 路径、PS2、四轮驱动、摇杆映射、S 曲线与反转窗口、DWT 时钟、校准保存和断电恢复模拟。测试不等同于真实电机或 Keil 编译验证。

## 下层车 UART、灯控与固件一致性

**2026-10-13：**两端烧录并独立读回，三轮 ON/OFF 得到全部六次 ACK，队员目视确认亮灭；ESP32 复位后重新握手通过。原始记录如下：

| 记录 | 含义 |
|---|---|
| [首次三轮灯控](evidence/board-link-led-20261013.json) | 六次 ACK，测试前后会话不变，错误计数为零 |
| [复位后运行](evidence/board-link-post-reset-20261013.json) | 复位后再次显示 connected=1 |
| [再次三轮灯控](evidence/board-link-led-repeat-20261013.json) | 六次 ACK，错误计数无增加 |
| [STM32 无独立电源时的失败记录](evidence/board-link-unpowered-stm32-20261013.json) | connected=0，不能把灯亮当作正常供电或通信依据 |
| [固件与读回摘要](evidence/board-link-readback-summary.json) | 10-16 对保留产物和独立读回重新计算哈希；包含首次 STM32 读回不一致和后续 SWD 间歇失败的已知情况 |

JSON 的 `optical_confirmation` 仍保留原始“需要用户观察”字段；队员后来在对话中确认看见亮灭，本文单独记录这一来源，不修改原始文件伪造自动光学测量。

**2026-10-16 上传前复验：**

```powershell
$env:CC='<主机 GCC 路径>'
$env:SANITIZE='0' # 本机缺 UBSan；不代表 sanitizer 通过
python firmware/tests/run_tests.py
python sample-mecanum-car-study/tests/run_tests.py
python sample-mecanum-car-study/build_firmware.py --target board-link --toolchain '<Arm GNU bin目录>'
pio run -d firmware/esp32 -e board-link
```

两套主机测试和两个 `board-link` 目标构建通过。共享 C 测试覆盖分帧/CRC、噪声、重放/乱序/旧会话、有效期边界、重连、时间回绕、编码器有效位、引脚资源无重叠、灯控 ACK 匹配与 500ms 超时，以及 100000 个确定性噪声字节。UBSan 因缺 `-lubsan` 未执行，不计为通过。

| 目标 | 工具与版本 | BIN | SHA256 |
|---|---|---:|---|
| STM32 `board-link` | Arm GNU 15.2.1 | 5376 字节 | `b56641de24ed48b9aa50830fbf04984fd336244e3423139bd8361cbe6b3c43f8` |
| ESP32 `board-link` | Espressif32 7.1.3、Arduino 2.0.17 | 289760 字节 | `7403d83eb69d8f5bc8611f8e44003bca0cd8963597f4f7d116f97b7f24c10149` |

这些 SHA 与此前已烧录并独立读回的版本一致。上传任务仅在电脑上复验，没有重新烧录或再次实物测试。

## 中期功能申报与待补证据

**2026-10-16 核对：**

| 项目 | 当前证据与限制 |
|---|---|
| 上层行驶、横移过桥 | 队员确认；需对应实物配置及合格视频，不把旧视频当全部验收 |
| 下层建模、底盘、机构、料仓 | 团队确认完成；最新模型待上传，尺寸/重量/运动范围待核对 |
| 下层电源 | 5V 支路已装配，12V 升压未完成；带载电源能力未验收 |
| 下层双板 UART 与灯控 | 已实现、构建、烧录读回及实物灯光确认 |
| 下层四轮驱动 | 电机输出固定禁止，不能申报整车运动已完成 |
| 编码器、闭环、自动路径 | 引脚与计数传输接口预留，真实采样/PID/路径未实现 |
| BLE 与手机面板 | 尚未实现 |
| 舵机联动、多舵机指标、方块携带 | 接口预留，当前型号/数量/动作/带块实测未核对；不从旧模型推定满足 |
| 外廓、重量、跌落、成本 | 需实际测量、成本/借用件表和对应材料；当前不宣称合格 |

## 开源使用说明

主机测试执行本仓库的真实 C 模块，上层基于 Serialist 开源小车，下层使用 ST/CMSIS 和 Espressif 框架。测试工具包括 Python、GCC、PlatformIO 和硬件测试时的 pyserial；许可证与来源见 [第三方清单](05-open-source.md)。原始 Flash 不上传，公开的是通信日志、源码及复算摘要，不把本地备份路径当作评审可访问的证据。
