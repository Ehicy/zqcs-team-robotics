# 证据索引：下层车台架首测（2026-10-16 / 17）

本目录是 `docs/10-lower-motor-bench-issue-20261017.md` 的证据附件。
所有数据来自实测；**本轮未发送任何 ARM / DRIVE / PWM 指令，未让电机动作。**

| 文件 | 内容 | 采集方式 |
|---|---|---|
| `esp32-console-transcript.txt` | COM7 控制台关键会话原文：固件身份、旧灯控对比、链路健康/失效判据、六次 LED 往返 | `.NET SerialPort` @115200 8N1，DTR/RTS=false |
| `stm32-register-reads.md` | RAM 标志位、RCC/IWDG/DWT 寄存器、内核寄存器的读取命令与解码 | `STM32_Programmer_CLI` 只读 `-u` / `-coreReg` |
| `stm32-ram-dumps/` | 上述读取的原始字节 | 同上 |
| `chip-link-test-20261016.json` | 2026-10-16 19:02 的成功记录：`board-link` 下 6 次 LED 往返、序号递增、错误计数不变 | 见 `verify_chip_link_after_esp32_flash.py` |
| `chip-link-test-after-esp32-flash.json` | 本轮同一验证器在 ESP32 换代后的结果（同时暴露 6.3 的长输出抖动） | 同上 |
| `verify_chip_link_after_esp32_flash.py` | 验证器本体。由 `tools/verify_live_chip_link_20261016.py` 派生，**仅两处改动**：输出路径改为新文件（避免覆盖 19:02 的成功记录）、固件名断言从 `crtc-board-link-led-v1` 放宽为 `crtc-lower-pid-v2` | — |
| `artifact-hashes.txt` | ESP32（bootloader/partitions/firmware）与 STM32（`lower-motor.hex` / `board-link.hex` / 备份 bin）的 SHA256 与字节数 | `Get-FileHash` |

## 关键结论速查

1. **`lower-motor` 不可用**：烧入后 ESP32 侧 `connected=0` 且 `rx_bytes` 增量恒为 0。
2. **根因**：`timing.c` 的 `iwdg_init()` 返回 0（`IWDG_PR/RLR` 写入未生效，`IWDG_SR` 不归零）
   → `timing_ready=0` → `main.c:124/199` 每轮 `transport_fault()` 拆链路。
3. **已排除**：接线、供电、ESP32 固件、链路角色、协议版本、CubeProgrammer 操作。
4. **同一 ESP32 + 同一接线，仅切换 STM32 目标即可在"在线/不可用"间翻转** —— 这是最有力的定位证据。

## 复现的最小命令

```powershell
# 1) 进问题态
STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst freq=100 `
  -d sample-mecanum-car-study/build/lower-motor/lower-motor.hex -v -hardRst -run
# 2) 在 COM7 上发 INFO：connected=0，反复采样 rx_bytes 不增长

# 3) 进正常态
STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst freq=100 `
  -d sample-mecanum-car-study/build/board-link/board-link.hex -v -hardRst -run
# 4) 在 COM7 上发 INFO：connected=1，rx_bytes 每 2 秒约 +2300；LED ON 得到 ACK
```

> 注意：第 1、3 步的 `-hardRst -run` 不能省，否则内核会停在 halt（见报告 6.1）。
