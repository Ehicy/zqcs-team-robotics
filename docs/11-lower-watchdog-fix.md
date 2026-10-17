# lower-motor 看门狗启动失联：复现、修复与实板验证

日期：2026-10-17。对应 [PR #6 的台架问题报告](https://github.com/Seroliya/zuiqiangchusheng/pull/6)。

**已复现并修复 `lower-motor` 启动后无法握手的问题。当前 STM32 留在修正版，独立读回与构建 BIN 一致；实际电机动作、四轮校准、闭环调参和手机 BLE 操作仍未验收。**

## 复现条件与根因

本地测试基线为 `30019c4e91b9aefbf0ee272577e8784e1fd9d57d`。与 PR 头 `97b631d76df932e2efe4dae2a7919c5fbb6dec17` 核对，`lower-motor/main.c`、`timing.c`、`board_link.h`、`board_link_node.c` 的 Git blob 一致。PR 报告还提到其工作树有未提交修改，不能据此声称两次测试使用完全相同的产物。

ESP32 实机为 `crtc-lower-pid-v2 source=BLE`，本次从 COM4 读取，保持原固件；STM32 经 ST-Link 连接，工具报告目标电压 3.25V。UART 沿用 GPIO4 TX→PA3 RX、GPIO6 RX←PA2 TX。

先保存 STM32 前 64KiB，再将原版 `lower-motor` 编译、下载、校验、复位运行。实测 `connected=0`、灯控返回 `ERR LED LINK_OR_BUSY`。两次 SWD 读取结果：

| 项目 | 原版结果 |
|---|---|
| `IWDG_PR / RLR / SR` | `0 / 0xFFF / 3`，配置未同步完成 |
| `RCC_CSR` | `0x1C000003`，LSI 就绪 |
| `cycles_per_us` | 72 |
| `iwdg_ready / timing_ready` | 0 / 0 |
| `milliseconds` | 62311 → 96399，时基在推进 |
| `board_link_transport_errors` | 122078 → 188888，主循环持续撤销链路 |

原初始化在启动 IWDG **之前**等待 PR/RLR 更新，实际板上等待超时；`delay_init()` 提前返回。随后主循环每次检查到 watchdog fault 都调用 `transport_fault()`，持续清接收队列和会话，导致无法建立通信。

## 修复行为

1. IWDG 改为 **START → 解锁 → 配置 PR/RLR → 有界等待同步 → 校验读回 → RELOAD**；显式清除 debug-freeze 位。先单独修改启动顺序上板，已得到 `PR=3、RLR=249、SR=0`，握手与灯控恢复。
2. SysTick 就绪与 IWDG 配置成功分别记录。已启动的 IWDG 即使配置失败，完整运行的诊断主循环仍可喂狗；电机门禁继续要求 IWDG 成功，不能降级放行。
3. watchdog fault 首次出现时立即停止并撤销旧会话；持续故障保留 UART、遥测与灯控，上报故障码 **6**。速度环、开环和最终 HAL 都禁止输出。
4. 普通 STOP、重连和心跳不清锁存。停车后显式 CLEAR 可以清除运行期锁存；IWDG 初始化失败不能通过 CLEAR 绕过。清故障不自动解锁或继续旧动作。

启动顺序参考 [ST 官方 STM32F1 HAL IWDG 实现](https://github.com/STMicroelectronics/stm32f1xx-hal-driver/blob/master/Src/stm32f1xx_hal_iwdg.c)，沿用本项目裸机结构，没有引入 HAL 依赖。PR 中同类尝试失败的历史记录保留；本次结论来自当前板上编译、写入及寄存器读回，不推断当时失败原因。

## 验证结果

原始串口记录、寄存器输出、固件哈希和测试边界统一保存于 [实板证据 JSON](evidence/lower-watchdog-20261017.json)。

| 测试 | 结果 |
|---|---|
| STM32 构建、下载验证、独立 Flash 读回 | 通过；BIN 13668 字节，SHA256 `e7105a361634472597f6fe78f0fe749b36773de42c8cbc5304e2d6d2c03a28b5` |
| 正常运行、软件看门狗锁存、STOP 后仍锁存、IWDG 不可用、持续故障、复位恢复 | 六阶段通过；48 次 INFO、36 次灯控 ACK，读取带 CRC 和当前会话的 SPEED 帧验证故障码 |
| 故障中的硬件门禁 | 各阶段读取 PB15 的输入电平和输出锁存，均为低；持续故障不会持续增加拆链路计数 |
| 三次软件复位 | 每次 IWDG 配置读回正确，重新握手，灯控 ON/OFF 均有 ACK |
| 停止 IWDG 喂狗 | 通过 RAM 故障注入禁止 RELOAD；`RCC_CSR` 从 `0x1C000003` 变为 `0x3C000003`，出现 IWDG 复位标志，运行计时重置，STBY 保持低 |
| 共享协议与桥接 | 主机测试通过；新增 watchdog fault 遥测、禁止 ARM、故障中灯控及恢复不自动解锁用例 |
| 轮速核心、网页 | 8 组轮速测试、15 组网页测试及脚本语法检查通过；均不代表实车验收 |

故障注入直接修改 RAM 中的 `watchdog_latched` 或 `iwdg_ready`，验证运行期门禁及诊断路径；没有人为破坏 LSI 或实际电源。IWDG 停止喂狗测试证明会复位，未测量其精确超时时间。没有发送 ARM/DRIVE/非零 PWM，没有确认电机转动、编码器物理接线或机械静止。UBSan 缺运行库，未执行。

## 对原问题报告的补充

- 本次原版故障态的 `rx_bytes` 仍增长，`board_link_tx_frames` 也增长，但会话无法建立。通信判断应结合稳定会话、状态、有效应答和错误增量，不能只看收到字节。
- 本次使用 `mode=HOTPLUG` 读取时 RAM 计时持续推进；连接命令打印 `Reset mode: Software reset` 不等于已经复位。读取后明确恢复运行，下载/恢复固件时才显式复位，避免将调试操作干扰混入结论。
- `-halt` 后退出工具未形成可证明的持续停核，初次停喂狗试验不具备判定条件；改用禁止 RELOAD 的 RAM 注入后确认 IWDG 复位。
- CubeProgrammer 对 `-w8` 的裸值 `1` 曾返回成功却未注入 0x01，测试脚本改用显式十六进制并读回校验。该次测试不计通过，失败记录仍在证据中。
- 恢复原始备份作对照时，连续 HELP/ENC/INFO 与灯控的 17 次状态查询均在线、错误计数无新增；未复现 PR 所述长回复导致的短时掉线。这只是本轮采样结果。

## 复测入口

`firmware/tests/test_watchdog_hardware.py` 要求精确匹配的 ELF/BIN、ESP32 USB 身份及 ST-Link 序号，运行前先核对板内 Flash。默认仅诊断；`--inject-fault` 才修改 RAM，结束时复位恢复。测试结束保留修正版固件和默认未确认的四轮校准。

```powershell
python firmware/tests/test_watchdog_hardware.py `
  --port <ESP串口> --serial <ESP_USB序列号> --stlink <下载器序号> `
  --cli <STM32_Programmer_CLI.exe> --nm <arm-none-eabi-nm.exe> `
  --elf sample-mecanum-car-study/build/lower-motor/lower-motor.elf `
  --report <本地结果.json> --inject-fault
```

STM32 编译用 Arm GNU 15.2.1，下载工具 STM32CubeProgrammer 2.23.0；测试依赖 pyserial。来源和许可见 [第三方清单](05-open-source.md)。
