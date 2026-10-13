# 两块 F103 的 USB / 串口台架测试

这份程序用于两块独立的 STM32F103C8T6 测试板。用户已指定第一块作为主板、下一块作为副板：主板运行已经实测的 USB / 串口转发固件，副板烧录单独的命令应答固件。电脑通过主板控制副板的 PC13 灯，并读取副板芯片编号；两块用各自的芯片唯一编号区分。

双机联动、运行接线与 USB 烧录规划见[方案说明](../../docs/DUAL_F103_USB_PROGRAMMING.zh-CN.md)。**当前 USB 是 CDC 通信接口，两板已通过 ST-Link 烧录；经主板 USB 烧录副板及主板 USB 自升级均尚未实现。**

## 1. 先连第一块

1. 只接 ST-Link：SWDIO→PA13、SWCLK→PA14、GND→GND；板未另行供电时，可由 ST-Link 的 3.3V 接核心板 3V3 供电。不要把 5V 接到 3V3。
2. 用 ST-Link 烧录 `build/usb-board-test/usb-board-test.hex`，校验后复位。
3. **拔掉 ST-Link→核心板的供电线**，SWDIO、SWCLK、GND 可以保留；用 USB 数据线连接核心板自己的 USB 接口到电脑，由 USB 供电。
4. Windows 应新增一个 COM 端口，测试设备 VID:PID 为 `CAFE:4001`。不要把原有蓝牙 COM 口当成这块板。
5. 默认 PC13 用户灯：USB 未配置时每 250ms 切换，已配置时每 1000ms 切换。电源灯和用户灯不是同一个灯。

适用前提：外部晶振 8MHz、板载 USB 接口连接 PA11/PA12、D+ 有适当外部上拉、PC13 用户灯低电平点亮。程序配 PLL 为 72MHz、USB 为 48MHz；外部晶振不起振时用户灯快速闪烁，且不启用 USB。不能用 LED 状态代替 USB 收发验收。

## 2. 电脑验证

安装 Python 依赖 `pyserial==3.5`。串口设置为 115200、8N1，并启用 DTR；USB 虚拟串口的物理速率由 USB 决定。

在 `sample-mecanum-car-study` 目录运行：

```powershell
python -m pip install pyserial==3.5
python examples/usb-board-test/host_test.py --list
python examples/usb-board-test/host_test.py --port COM7 --report build/usb-board-test/usb-report.json
```

把 `COM7` 换成实际检测到的端口。脚本只会打开 VID:PID 匹配的测试板，默认检查 100 次 PING、100 次 ECHO、灯控制、非法命令和长输入后的恢复。它不下载固件。

本机第一块已识别为 **COM3** 并通过以上检查。本机测试工具的 Python 已单独安装依赖，可直接运行：

```powershell
& 'D:/DEVELOP/CQU_AI/tools/usb-test-venv/Scripts/python.exe' examples/usb-board-test/host_test.py --port COM3
```

也可以在串口工具里逐条发送以下文本，末尾换行：

| 命令 | 回复 / 效果 |
|---|---|
| `PING` | `PONG` |
| `INFO` 或 `ID` | 固件版本、芯片 UID、运行毫秒数、收发错误计数 |
| `ECHO hello` | `ECHO hello` |
| `LED ON` / `LED OFF` | 点亮 / 熄灭 PC13 用户灯 |
| `LED TOGGLE` | 用户灯切换一次 |
| `LED AUTO` | 恢复自动闪灯 |
| `UART hello` | 回复 `OK UART QUEUED`，并从 PA2 发出 `hello` 加回车换行 |
| `HELP` | 显示命令表 |

`LED ON/OFF/TOGGLE` 后自动闪灯暂停。单条输入最多 127 字节，不含末尾换行。`OK UART QUEUED` 只代表已排入发送队列，必须在对面收到 `UART_RX hello` 才算链路通过。

## 3. 副板与主副板通信

副板固件为 `build/usb-slave-test/usb-slave-test.hex`，2026-10-09 已烧录并校验成功；UID `02040200505400193638414E`，曾枚举为 COM4。主板 `usb-board-test-v1` 也已烧录并校验，UID 为 `02430207505400113638414E`，已分配端口 COM3。已通过主板 COM3 实测串口 PONG、副板编号查询及副板灯切换，简短测试 PASS；独立副板 USB 收发未单独完成。

首次部署或更换副板时，先断电，把 ST-Link 的 SWDIO、SWCLK、GND 接到第二块对应接口；副板可选择 USB 或 ST-Link 3.3V 单独供电。读取第二块 UID，确认不等于上述主板，再备份、下载副板 HEX、校验并复位。接 USB 时移除 ST-Link 的供电线。副板 USB 产品名为 `F103 USB Slave Test`，仍使用 `CAFE:4001`。

烧录后，断电接三根板间信号线：

| 第一块 A | 第二块 B |
|---|---|
| PA2（TX） | PA3（RX） |
| PA3（RX） | PA2（TX） |
| GND | GND |

主板用自己的 USB 连接电脑。副板可由自己的 USB 或 ST-Link 3.3V 独立供电，串口指令不依赖副板 USB 连电脑。**不要把两块的 3V3 输出相连，也不要把 PA2 接 PA2。** 这里的 USART2 是 3.3V TTL，不能接 RS-232 电平。

```powershell
& 'D:/DEVELOP/CQU_AI/tools/usb-test-venv/Scripts/python.exe' examples/usb-board-test/slave_test.py --port COM3 --master-uid 02430207505400113638414E --report build/usb-slave-test/link-report.json
```

完整测试脚本先查询副板，要求返回副板固件名称及不同于主板的 UID，再检查 100 次请求应答、100 次文本回传、灯状态、无效报文后的恢复，以及两板串口错误计数。用户当前要求简短验证，优先用下面的 `--command 'LED TOGGLE'`，一次查询编号、灯控和状态即可。正确运行结果为 `PASS`；接线/供电/固件缺失时会超时或报错，`OK UART QUEUED` 不能作为双板通过的依据。

错误计数从上电开始累积，换线时可能留下旧错误。`slave_test.py` 记录前后读数，要求本次 UART / USB 错误不增加；不会把历史计数清零或隐藏。当前简短测试：主板历史 UART 错误2，副板0，本次各项新增错误均0。

单独控制副板灯：

```powershell
& 'D:/DEVELOP/CQU_AI/tools/usb-test-venv/Scripts/python.exe' examples/usb-board-test/slave_test.py --port COM3 --command 'LED TOGGLE'
```

也可以在主板串口工具里发送一行：

```text
UART REQ 00000001 LED TOGGLE
```

主板先回复 `OK UART QUEUED`；副板执行后，主板收到并报告 `UART_RX RSP 00000001 OK LED 1`（或 `0`）。没有 `UART REQ ...` 前缀的 `LED TOGGLE` 仍控制主板自己的灯。

板间格式为 `REQ <8位非零十六进制编号> <命令>`，响应为 `RSP <相同编号> <结果>`，每行以回车换行结束。副板接受 PING、INFO/ID、ECHO、LED 命令及 HELP，忽略无效编号、非请求行和过长行。它不会执行收到的响应行，也不会通过请求再次转发 UART 命令。

UART 的 INFO 返回固件、角色、UID、错误计数和灯状态；USB 的 INFO 另含运行时间与收发统计。这个台架协议用于通信和灯控制，尚未连接电机、实现新底盘控制或失联停车。原 `host_test.py --peer-port` 仅用于两块都运行转发固件的测试。

## 4. 重新编译

本目录含应用代码；USB 协议使用 [TinyUSB 0.18.0](https://github.com/hathach/tinyusb/tree/0.18.0)，库本体不复制进仓库。依赖放在仓库外的 `tools` 目录：

- Arm GNU Toolchain，已验证 GCC 14.3.1。
- TinyUSB 固定提交 `86ad6e56c1700e85f1c5678607a762cfe3aa2f47`（0.18.0）。
- [ST CMSIS Device F1](https://github.com/STMicroelectronics/cmsis-device-f1) 固定提交 `c8e9a4a4f16b6d2cb2a2083cbe5161025280fb22`。
- [ARM CMSIS 5.9.0](https://github.com/ARM-software/CMSIS_5/tree/5.9.0) 的 `CMSIS/Core/Include`，只需 `core_cm3.h`、`cmsis_version.h`、`cmsis_compiler.h`、`cmsis_gcc.h`、`mpu_armv7.h`。

本机已安装上述依赖，在此项目目录使用：

```powershell
python build_usb_test.py --toolchain D:/DEVELOP/CQU_AI/tools/arm-gnu-14.3/bin --tinyusb D:/DEVELOP/CQU_AI/tools/tinyusb-0.18.0 --cmsis-device D:/DEVELOP/CQU_AI/tools/cmsis-device-f1 --cmsis-core D:/DEVELOP/CQU_AI/tools/cmsis-core-5.9.0
```

默认 `--role bridge` 构建主板转发版。构建副板时追加 `--role slave`：

```powershell
python build_usb_test.py --role slave --toolchain D:/DEVELOP/CQU_AI/tools/arm-gnu-14.3/bin --tinyusb D:/DEVELOP/CQU_AI/tools/tinyusb-0.18.0 --cmsis-device D:/DEVELOP/CQU_AI/tools/cmsis-device-f1 --cmsis-core D:/DEVELOP/CQU_AI/tools/cmsis-core-5.9.0
```

两种角色分别输出到 `build/usb-board-test/` 和 `build/usb-slave-test/`，各含 ELF、HEX、BIN、MAP 及包含依赖版本、链接脚本、头文件和固件 SHA256 的 `manifest.json`。台架固件使用本目录独立的 `stm32f103c8.ld`，严格限制在 C8 的 64KiB Flash / 20KiB RAM，并至少留 2KiB 栈；不依赖下载器报告的额外 Flash 容量。原底盘链接脚本仍保留校准页。构建脚本不操作硬件。

下载示例（先核对目标板）：

```powershell
& 'D:/programe/stm32/bin/STM32_Programmer_CLI.exe' -c port=SWD freq=240 mode=NORMAL -w build/usb-slave-test/usb-slave-test.hex -v -rst
```

软件用到 PA11/PA12（USB）、PA2/PA3（USART2）、PC13（灯），保留 PA13/PA14 的 SWD。双 F103 规划里也采用 USART2，可复用这三根板间通信线。测试固件不会初始化电机、舵机、PS2 或 IMU。

第三方许可见 `THIRD_PARTY_LICENSES.txt`；测试 VID/PID 来自 TinyUSB 示例，只用于本地台架测试。
