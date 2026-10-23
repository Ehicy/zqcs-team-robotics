# 2026-10-22 C3 直控固件与记录

当前安装：STM32 `dashboard-motor`（诊断标记 `0xC3D61022`），C3 `crtc-c3-direct-pwm-v2`。

- `stm32/dashboard-motor.hex` / `.bin`：PA 侧四轮电机、USART3、四轮编码器；STM32 用 ST-Link 直烧到 `0x08000000`。
- `esp32/`：LuatOS CORE ESP32-C3 / CH343，4MB / DIO。`firmware/tools/flash_c3_dashboard.ps1 -Port COM9` 使用 bootloader `0x0`、partitions `0x8000`、框架 `boot_app0.bin` `0xe000`、APP `0x10000`。
- `direct-runtime-info.txt`、`stm32-direct-flash.log`：当前版本 INFO、UART 和 STM32 写入验证记录。
- 其余诊断和构建日志保留当天较早阶段的状态，用于排查过程；当前版本身份以上述两个记录为准。
- `dashboard-direct-ready.png`：已连接的 PWM 直控页面。默认输出比例20%，可调到100%；实际速度由编码器回传，不把占空比当成轮速。

当天已完成两板写入、UART 双向握手和 Dashboard 蓝牙连接。本次 PR 整理只做软件复测，没有连接、复位或驱动硬件；当前版本仍需实车核对四轮方向、负载表现和编码器换算。

重建命令和全部引脚见 `docs/16-c3-dashboard-encoders.md`。调试 ELF、主机测试 EXE 和原 Flash 备份不作为发布文件。
