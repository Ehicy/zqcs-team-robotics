# 下层车台架首测问题报告：`lower-motor` 的看门狗初始化缺陷会让板间链路自毁

日期：2026-10-17（事件发生于 2026-10-16 22:20 – 2026-10-17 01:20）
范围：下层车 STM32 `lower-motor` 目标首次实板烧录；同时交付桌面 BLE 遥控台。

## 摘要

**结论：`sample-mecanum-car-study/examples/lower-motor/` 这份从未实板验收过的目标，第一次上板就暴露一个缺陷——它的看门狗初始化在本板上失败，而失败信号被同时用于"拆掉板间通信链路"，导致板子表现得像完全死掉。**

| 项目 | 结果 |
|---|---|
| 现象 | 烧入 `lower-motor` 后，ESP32 侧 `connected=0`、`rx_bytes` 完全冻结、`LED ON` 返回 `ERR LED LINK_OR_BUSY` |
| 直接原因 | `timing.c` 的 `iwdg_init()` 返回 0 → `timing_ready` 永远为 0 |
| 放大原因 | `main.c:124/199` 把 `lower_motor_watchdog_fault()` 当作**通信链路**的失效条件，于是每一轮主循环都执行 `transport_fault()` 拆链路 |
| 影响 | `lower-motor` 无法用于实车；BLE 遥控台无法驱动电机 |
| 不是原因 | 接线、供电、ESP32 固件、CubeProgrammer 操作（这三项都单独排除过，见第 3 节） |
| 建议 | 见第 5 节：允许链路存活 + 保持输出禁止；并修 `iwdg_init()` 的启动顺序 |

## 1. 复现前提

- ESP32-S3 N16R8，原生 USB `COM7`，固件 `crtc-lower-pid-v2 source=BLE`（`board-link` 环境，2026-10-16 由当前工作树构建烧入）。
- STM32F103C8T6，ST-Link（VID_0483&PID_3748）SWD 烧录。
- 烧录工具：`D:\programe\stm32\bin\STM32_Programmer_CLI.exe`（v2.23.0）。
- 固件构建：
  - ESP32：`PLATFORMIO_CORE_DIR=<core> python firmware/build_esp32.py -e board-link`
  - STM32：`python sample-mecanum-car-study/build_firmware.py --target lower-motor --toolchain <arm-gnu-14.3/bin>`

复现命令（注意结尾必须带 `-hardRst -run`，理由见 6.1）：

```powershell
STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst freq=100 `
  -d sample-mecanum-car-study/build/lower-motor/lower-motor.hex -v -hardRst -run
```

随后在 `COM7` 上发 `INFO`：

```text
INFO firmware=crtc-lower-pid-v2 source=BLE ps2_state=0 ps2_mode=00 transport=1
     connected=0 token=00000000 flags=0 encoder_mask=0 parser_errors=0 rejected=186
     rx_bytes=212937 tx_bytes=... tx_frames=... uart_faults=0 last_uart_event=-1
     rx_level=1 uptime_ms=... tx_gpio=4 rx_gpio=6
```

`rx_bytes` 在多次采样间**增量恒为 0**，即 STM32 一个字节都没有发出。同一块板换回 `board-link` 目标后链路立刻恢复（`connected=1`、`rx_bytes` 每 2 秒约 +2300、`LED ON → ACK LED=1`）。

## 2. 现象时序

| 动作 | ESP32 侧观察 |
|---|---|
| 烧 ESP32 `board-link`，STM32 仍旧镜像 | `connected=1 token=DD7894FF flags=7` ✅ |
| 烧 STM32 `lower-motor` | `connected=0`、`rejected ≈ tx_frames`、`rx_bytes` 冻结 ❌ |
| 用烧录前备份完整恢复 STM32 | 仍然 `connected=0` ❌（当时误判为物理问题） |
| 烧 STM32 `board-link`（同源重建） | `connected=1`，19.5 秒 13 次采样全部在线 ✅ |
| 再烧 `lower-motor`（含本文修正尝试） | 又回到 `connected=0`、`rx_bytes` 冻结 ❌ |

该表本身就是最有力的定位证据：**同一个 ESP32、同一套接线、同一根 USB 线，只切换 STM32 的目标，链路就在"可用 / 不可用"之间翻转。**

## 3. 被排除的假设

| 假设 | 排除方式 | 结论 |
|---|---|---|
| 板间接线断了 | 换上同源 `board-link` 后链路立刻稳定在线 | 排除 |
| 供电不足 / 反灌 | ST-Link 测得目标电压 3.25V；`board-link` 下长时间稳定运行 | 排除 |
| ESP32 固件有问题 | 同一份 ESP32 固件在 `board-link` 下工作正常 | 排除 |
| 链路角色配错（两端都当 client） | `esp32/src/main.cpp:208` 用 `LINK_CLIENT`，两个 STM32 目标都用 `LINK_CONTROLLER` | 排除 |
| 协议版本不配套 | 两端改为同一棵工作树构建后仍复现 | 排除（但"必须同源"这一条另有价值，见 6.2） |
| CubeProgrammer 把内核留在 halt | 确实存在，且已按 6.1 规避；规避后仍复现 | **不是本次原因** |

## 4. 根因证据

### 4.1 STM32 RAM 标志位（ST-Link 读取）

符号地址取自 `build/lower-motor/lower-motor.elf`（`arm-none-eabi-nm`）：

| 符号 | 地址 | 读到的值 |
|---|---|---|
| `cycles_per_us` | `0x2000055c` | `72`（SystemCoreClock=72MHz，正常） |
| `iwdg_ready` | `0x20000560` | **`0x00`** |
| `timing_ready` | `0x20000561` | **`0x00`** |
| `last_feed_ms` | `0x20000564` | `0` |
| `milliseconds` | `0x20000568` | 持续增长（SysTick 正常） |

### 4.2 外设寄存器

| 寄存器 | 地址 | 值 | 判读 |
|---|---|---|---|
| `RCC_CSR` | `0x40021024` | `0x1C000003` | `LSION=1`、`LSIRDY=1` → **LSI 已就绪，不是 LSI 起不来** |
| `IWDG_SR` | `0x4000300C` | `0x0003` | PVU/RVU 一直挂着，更新从未完成 |
| `IWDG_PR` | `0x40003004` | `0x0000` | 仍是复位默认，**代码写的 `PR=3` 未生效** |
| `IWDG_RLR` | `0x40003008` | `0x0FFF` | 仍是复位默认，**代码写的 `RLR=249` 未生效** |
| `DWT_CTRL` | `0xE0001000` | `0x40000001` | CYCCNTENA 已置位，DWT 可用 |
| `DWT_CYCCNT` | `0xE0001004` | 1.5 秒推进 119,090,650 | 计数器在走，**超时机制本身没问题** |

即：**LSI 正常、DWT 正常、SysTick 正常，但 IWDG 的预分频/重装值写不进去，`IWDG_SR` 也不归零。**

### 4.3 出错代码路径

`examples/lower-motor/timing.c`：

```c
static uint8_t iwdg_init(void)
{
    RCC->CSR |= RCC_CSR_LSION;
    ...                                    /* 等 LSIRDY，实测会通过 */
    IWDG->KR = IWDG_KEY_ACCESS;            /* 0x5555 */
    IWDG->PR = 3U;                         /* ← 实测未生效 */
    IWDG->RLR = 249U;                      /* ← 实测未生效 */
    while (IWDG->SR != 0U) {               /* ← 因此这里必然超时 */
        if ((uint32_t)(DWT_CYCCNT_REG - start) >= cycles_per_us * 20000U) return 0;
    }
    IWDG->KR = IWDG_KEY_RELOAD;
    IWDG->KR = IWDG_KEY_START;
    return 1;
}
```

`delay_init()` 在 `if (!iwdg_init()) return;` 处提前返回，`timing_ready` 与 `iwdg_ready` 保持 0。

放大链路（`examples/lower-motor/main.c`）：

```c
uint8_t lower_motor_watchdog_fault(void)      /* timing.c */
{ return (uint8_t)(!timing_ready || !iwdg_ready || watchdog_latched); }

while (1) {
    now = delay_millis(); link_node_tick(&controller, now);
    if (rx_fault || lower_motor_watchdog_fault()) transport_fault(now);   /* main.c:124 */
    ...
    if (motors.fault || ... || lower_motor_watchdog_fault()) transport_fault(now);  /* main.c:199 */
    ...
}
```

`transport_fault()` 内部会清接收队列并调用 `link_node_transport_error()`，于是**每一次循环都把刚刚建立/将要建立的会话拆掉**，链路永远无法握手成功——这就是"板子看起来完全死了"的原因。

## 5. 建议修法

### 5.1 架构层面（建议优先）

`lower_motor_watchdog_fault()` 目前承担了两个性质不同的职责：

- `main.c:221` —— 门禁**电机输出**：看门狗没在保护就不许驱动。**这是正确的失效安全设计。**
- `main.c:124/199` —— 拆掉**板间通信链路**。**这是把"驱动安全"错当成了"通信健康"。**

后果是：一旦看门狗起不来，板子连自己的状态都发不出去，操作者拿不到任何诊断线索（这也是本次定位绕远路的主要原因——过程中曾先后误判为"接线断"和"内核卡死"）。

建议**解耦**：链路照常建立与维持，`lower_motor_watchdog_fault()` 只用于把 `board_link_outputs_inhibited` 锁在 1。这样即使 IWDG 有问题，也能通过 BLE/串口读到真实状态，而驱动侧依然是失效安全。

### 5.2 具体修法

1. **`iwdg_init()` 的启动顺序**：先 `IWDG->KR = IWDG_KEY_START` 让 IWDG 逻辑真正运行，再解锁写 `PR/RLR`，最后 `RELOAD`。
   *本轮已按此改动并实板验证：**仍复现**，见第 7 节。*
2. **`IWDG_SR` 等待失败不应等同"看门狗不存在"**：若 `PR/RLR` 未能写入，IWDG 仍会以复位默认（`PR=0`、`RLR=0x0FFF`）运行，其超时远长于设计值但仍是一个在跑的看门狗。此时更合理的做法是**记录该降级并继续**，而不是让整机失去通信。
3. **提供一条"无看门狗"诊断构建**：便于台架阶段区分"驱动故障"与"看门狗初始化故障"。

### 5.3 本板专项

`IWDG_PR`/`RLR` 写入不生效这一点，需要单独确认是否与**该 MCU 的 IWDG 兼容性/选项字节**有关（例如硬件看门狗选项会使 `PR/RLR` 只读）。这一点**本轮未验证**，不要当作结论。

## 6. 附带发现

### 6.1 `STM32_Programmer_CLI` 会把内核留在 halt

每次连接都会做软件复位并把内核停在 halt；只要该次调用**没有以 `-run` 收尾**（例如只读 `-coreReg`、只读内存、或中途被打断），STM32 就停在那里不执行任何代码。

**正确用法：每条 `STM32_Programmer_CLI` 调用都以 `-hardRst -run` 收尾，收尾后不要再连接它。** 本轮多次"莫名断链"由此而来。

### 6.2 两端固件必须同源

本地工作树相对上游 `main`（`1873cac`）有未提交改动，其中 `firmware/shared/board_link_node.c`、`board_link.h`、`board_link_config.h`、`remote_bridge.c`、`speed_control.c` 都参与链路。用 2026-10-13 的旧 STM32 镜像配新 ESP32 固件时链路不稳定；两端改为同一棵工作树构建后才稳定。这与 `docs/09-ps2-chassis-pid.md` 中"两端新固件需配套"一致。

### 6.3 控制台长输出会造成瞬时 `connected=0`

现成验证器跑完 6 次 LED 往返后，末尾那次 `INFO` 读到 `connected=0 token=0`，紧接着的 `LED OFF` 又立刻拿到 ACK。属 300ms 对端超时被长回复拖出的抖动，下一周期自愈。

**判定链路健康请以 `rx_bytes` 是否持续增长为准，不要用单次 `connected` 快照。**

### 6.4 旧 STM32 USB 程序不能烧录

用户记忆中的"USB 调试"是 PR #4 的 TinyUSB 诊断程序（`usb-board-test`），**没有实现 USB bootloader 或二进制烧录桥**；STM32 下载路径一直是 ST-Link SWD。

## 7. 本轮未完成 / 未验证

1. `IWDG_PR/RLR` 写入为何不生效——**未定位到根因**，只确认了现象。
2. 按 5.2.1 调整启动顺序后**仍然复现**，所以该改动本身不足以修复。
3. 12V 直供支路未装配，四只 MG370 无动力电，**"轮子真的转"本轮未验证**。
4. 四轮编码器方向/每圈计数/轮径仍为待实测初值（`confirmed` 默认 0）。
5. 本轮未发送任何 ARM/DRIVE/PWM 指令，未让电机动作。
6. `lower-motor` 与 `board-link` 均只做了构建 + 烧录 + 链路层验证，**未做电机/编码器实物验收**。

## 8. 证据清单

见 `docs/evidence/lower-car-20261017/`：

| 文件 | 内容 |
|---|---|
| `esp32-console-transcript.txt` | COM7 控制台关键会话原文（INFO/HELP/ENC/LED/ERR） |
| `stm32-register-reads.md` | 第 4 节寄存器读数的原始命令与解码 |
| `stm32-ram-dumps/*.bin` | ST-Link 读出的原始字节（`a/b/c/d1/d2/iwdg/iwdg2/rcc/w1/w2`） |
| `chip-link-test-20261016.json` | 2026-10-16 19:02 `board-link` 灯控 6 次 ACK 通过记录 |
| `chip-link-test-after-esp32-flash.json` | 本轮同一验证器在两次不同 STM32 目标下的结果 |
| `verify_chip_link_after_esp32_flash.py` | 该验证器（由 `tools/verify_live_chip_link_20261016.py` 派生，仅改固件名断言与输出路径） |
| `artifact-hashes.txt` | ESP32 / STM32 各产物的 SHA256 |

## 9. 交付物（同 PR）

- 桌面键盘遥控台：`firmware/web/dashboard.html`、`dashboard.css`、`dashboard.js`、`dashboard-input.js`
- 测试：`firmware/tests/test_web_dashboard.mjs`（18 组）、`firmware/tests/test_dashboard_page.py`（真实 Edge，49 项断言）
- 交互式网页：`docs/figures/chassis-wiring-editor.html`、`docs/figures/physical-pin-locator.html`

遥控台与既有手机面板共用同一份 `protocol.js`（同一 20 字节包、同一会话/解锁门禁、同一单在途写队列），未改动协议层。
