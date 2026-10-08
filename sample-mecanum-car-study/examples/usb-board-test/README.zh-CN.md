# 两块 F103 的 USB / 串口台架测试

这份程序用于两块独立的 STM32F103C8T6 测试板。两块可以烧同一个 HEX，各自用芯片唯一编号区分。它提供 USB 虚拟串口、PC13 灯控制和 USART2 转发，不是底盘控制固件。

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

## 3. 第二块与双板通信

先把第二块单独烧录、接 USB，验证能识别；两块的 UID 不同，Windows 会为它们分配各自的 COM 口。随后断电接三根信号线：

| 第一块 A | 第二块 B |
|---|---|
| PA2（TX） | PA3（RX） |
| PA3（RX） | PA2（TX） |
| GND | GND |

两块分别用自己的 USB 供电；**不要把两块的 3V3 输出相连，也不要把 PA2 接 PA2。** 这里的 USART2 是 3.3V TTL，不能接 RS-232 电平。

```powershell
python examples/usb-board-test/host_test.py --port COM7 --peer-port COM8 --report build/usb-board-test/dual-report.json
```

脚本检查两块 USB，并各方向发送 10 条串口消息。程序把收到的串口行报告给电脑，不会再从串口自动发回，避免循环转发。此测试尚不包含底盘控制、通信失联保护或 PID。

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

输出 ELF、HEX、BIN、MAP 和包含依赖版本、头文件及固件 SHA256 的 `manifest.json`。构建严格限制在 C8 的 64KiB Flash / 20KiB RAM，并至少留 2KiB 栈；不依赖下载器报告的额外 Flash 容量。构建脚本不操作硬件。

下载示例（先核对目标板）：

```powershell
& 'D:/programe/stm32/bin/STM32_Programmer_CLI.exe' -c port=SWD freq=240 mode=NORMAL -w build/usb-board-test/usb-board-test.hex -v -rst
```

软件用到 PA11/PA12（USB）、PA2/PA3（USART2）、PC13（灯），保留 PA13/PA14 的 SWD。双 F103 规划里也采用 USART2，可复用这三根板间通信线。测试固件不会初始化电机、舵机、PS2 或 IMU。

第三方许可见 `THIRD_PARTY_LICENSES.txt`；测试 VID/PID 来自 TinyUSB 示例，只用于本地台架测试。
