# STM32 寄存器与 RAM 读取记录（2026-10-17）

所有读取都用 ST-Link + `STM32_Programmer_CLI`，**只读**，未写任何寄存器。

## 0. 符号地址（来自 `build/lower-motor/lower-motor.elf`）

```powershell
arm-none-eabi-nm -n sample-mecanum-car-study/build/lower-motor/lower-motor.elf |
  Select-String 'cycles_per_us|iwdg_ready|timing_ready|milliseconds|last_feed_ms'
```

```text
2000055c b cycles_per_us
20000560 b iwdg_ready
20000561 b timing_ready
20000564 b last_feed_ms
20000568 b milliseconds
```

## 1. RAM 标志位（读 0x20000558，长度 0x18）

```powershell
STM32_Programmer_CLI -c port=SWD mode=hotplug freq=100 `
  -u 0x20000558 0x18 "$env:TEMP\lm\c.bin"
```

原始字节：

```text
00 01 00 00 48 00 00 00 00 00 00 00 00 00 00 00 94 25 00 00 e5 9b 01 de
```

解码：

| 偏移 | 符号 | 值 | 判读 |
|---|---|---|---|
| +0x04 | `cycles_per_us` | `72` | SystemCoreClock=72MHz，正常 |
| +0x08 | `iwdg_ready` | `0x00` | **IWDG 初始化未成功** |
| +0x09 | `timing_ready` | `0x00` | **时基未就绪** |
| +0x0c | `last_feed_ms` | `0` | 从未喂过狗 |
| +0x10 | `milliseconds` | `9620` / `47237` / `99519`（不同次读取） | SysTick 在跑 |

## 2. RCC 与 IWDG 寄存器

```powershell
STM32_Programmer_CLI -c port=SWD mode=hotplug freq=100 -u 0x40021024 4 "$env:TEMP\lm\rcc.bin"
STM32_Programmer_CLI -c port=SWD mode=hotplug freq=100 -u 0x40003000 16 "$env:TEMP\lm\iwdg.bin"
```

| 寄存器 | 地址 | 读到的值 | 判读 |
|---|---|---|---|
| `RCC_CSR` | `0x40021024` | `0x1C000003` | `LSION=1`、`LSIRDY=1` → LSI 已就绪 |
| `IWDG_KR` | `0x40003000` | `0x0000` | 只写寄存器，读回 0 属正常 |
| `IWDG_PR` | `0x40003004` | `0x0000` | 复位默认 → **`PR=3` 未写入** |
| `IWDG_RLR` | `0x40003008` | `0x0FFF` | 复位默认 → **`RLR=249` 未写入** |
| `IWDG_SR` | `0x4000300C` | `0x0003`（一次） / `0x0000`（另一次） | PVU/RVU 悬挂，更新未完成 |

## 3. DWT（用于排除"超时机制不工作"）

```powershell
STM32_Programmer_CLI -c port=SWD mode=hotplug freq=100 -u 0xE0001000 8 "$env:TEMP\lm\d1.bin"
# 间隔 1.5 秒再读一次
```

| 寄存器 | 第 1 次 | 第 2 次 | 判读 |
|---|---|---|---|
| `DWT_CTRL` | `0x40000001` | `0x40000001` | CYCCNTENA 已置位，DWT 可用 |
| `DWT_CYCCNT` | `0x218D1CB2` | `0x28A64A8C` | 1.5 秒推进 119,090,650 → **计数器正常** |

因此 `iwdg_init()` 里"等 `IWDG_SR` 归零直到 DWT 超时"的机制本身是可用的；
它返回 0 是因为 `IWDG_SR` 在该窗口内确实没有归零，而不是超时逻辑失灵。

## 4. 内核寄存器（用于排除"内核卡死"）

```powershell
STM32_Programmer_CLI -c port=SWD mode=hotplug freq=100 -coreReg
```

多次读到 `PC = 0x0800105C`。经 `arm-none-eabi-nm` + `objdump` 映射：

```text
0800105c <SysTick_Handler>:
 800105c:  4a08  ldr r2, [pc, #32]
```

即该 PC 是 **`SysTick_Handler` 的第一条指令**——1ms 周期中断被采样到，属正常现象，
**不能据此判定内核卡死**。这一点在定位过程中曾造成一次误判。

## 5. 重要操作注意事项

`STM32_Programmer_CLI` 每次连接都会软件复位并把内核停在 halt。**只读操作同样如此。**
因此：

1. 任何一次调用（包括只读 `-u` / `-coreReg`）之后，都要再单独发一条
   `-c port=SWD mode=UR reset=HWrst freq=100 -hardRst -run` 让 STM32 恢复运行；
2. 连续两次读取之间若不执行恢复，读到的 `milliseconds` / `DWT_CYCCNT` 会完全相同——
   那是内核停住，不代表程序死了。

## 6. 原始转储

`stm32-ram-dumps/` 保留本轮读出的原始字节：

| 文件 | 地址 | 长度 |
|---|---|---|
| `a.bin` | `0x20000000` | 16 |
| `b.bin` | `0x200004b8` | 4 |
| `c.bin` | `0x20000558` | 24 |
| `rcc.bin` | `0x40021024` | 4 |
| `iwdg.bin` / `iwdg2.bin` | `0x40003000` | 16 |
| `d1.bin` / `d2.bin` | `0xE0001000` | 8 |
| `w1.bin` / `w2.bin` | `0xE0001004` | 4 |
