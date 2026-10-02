# PS2 圆圈键翻转核心板 LED

这是 STM32F103C8T6 的独立测试入口，源文件为本目录 `main.c`。
启动时 PC13 用户灯闪三次后熄灭；每按下并松开一次右侧 **○（圆圈）**，
灯的亮灭状态翻转一次。按住按键不会连续翻转。

## 接线

| PS2 转接板丝印 | STM32 |
|---|---|
| DAT / DI | PB14，输入 |
| CMD / DO | PB15，输出 |
| CS / SEL / ATT | PB12，输出 |
| CLK | PB13，输出 |
| GND | GND，共地 |
| VCC | 按转接板规格供电；本次使用带 3.3V 稳压的转接板，输入 5V |

按丝印对应，不能把这张表的顺序当成排针物理顺序。裸接收器供电和转接板
输入电压不是一回事。本例使用核心板 **PC13 用户 LED，低电平亮**；核心板
POWER 灯以及接收器 POWER/RX 灯不受本例控制。如果换成其他核心板，应核对
用户 LED 的引脚和有效电平。

## 代码是怎么工作的

1. **初始化 GPIO 和延时。** `ps2_init()` 沿用仓库驱动的四根信号线，
   `led_init()` 把 PC13 配成推挽输出。输出低电平让 LED 亮，输出高电平让它灭。
2. **向接收器询问按键。** STM32 用 CS 选中接收器，CMD 发出 `01 42 00 …`，
   CLK 提供时钟，DAT 收回数据。每字节先传最低位。本例复用 `ps2_comm()`，
   将回应保存在 `ps2_led_debug.raw`，检查模式和固定标志 `5A`。
3. **识别圆圈键。** `raw[4]` 的 bit5 是 ○，原始值 **0 表示按下**：

   ```c
   down = ((raw[4] & 0x20U) == 0U);
   ```

   所有按键松开时，两个按键字节通常为 `FF FF`；只按 ○ 时，第二个通常为 `DF`。
4. **只在刚按下时翻转。** 保存上次状态 `previous_down`，当上次松开、本次按下，
   才执行一次翻转：

   ```c
   if (armed && down && !previous_down) {
       led_set(!ps2_led_debug.led_on);
       ps2_led_debug.circle_presses++;
   }
   previous_down = down;
   ```

   每轮约隔 20ms 读取。若只写 `if (down) 翻转`，按住时每一轮都会翻转，
   看起来就会乱闪。`armed` 要求上电和断线重连后先松开按键，防止误触发。

原小车程序的 `ps2_read()` 只接受 `73` 模拟模式。本例为了诊断还接受 `41`
数字模式和 `79` 压感模式；后者读取完整 21 字节。本例不负责模式协商。

## 编译与用 CubeProgrammer 下载

在 `sample-mecanum-car-study` 目录运行（需要 Python 和 Arm GNU 工具链）：

```powershell
python .\build_firmware.py --target ps2-led --toolchain 'D:\DEVELOP\CQU_AI\tools\arm-gnu-14.3\bin'
```

得到 `build\ps2-led\ps2-led.hex`、`.elf`、`.bin`。编译只是把 C 代码转换为机器代码，
这一步不会写入板子。不带 `--target ps2-led` 时仍编译原小车程序。

在 STM32CubeProgrammer 中：

1. 选择 ST-LINK、SWD；本次稳定连接设置为 **Under Reset，100 kHz**。
2. 点击 Connect，然后在下载页选择 `build\ps2-led\ps2-led.hex`。
3. 勾选下载后校验，执行 Download；成功后复位或运行。启动时应看到用户灯闪三次。
4. 松开所有键，按 ○ 并松开，再按 ○，灯应亮、灭交替。

ST-Link 的 SWDIO 对应 PA13，SWCLK 对应 PA14，GND 必须相连；
使用硬件复位连接时，NRST 也需要按实际调试器接口连接。

本机也可以用 CLI 下载；在同一目录运行：

```powershell
& 'D:\programe\stm32\bin\STM32_Programmer_CLI.exe' -c port=SWD mode=UR reset=HWrst freq=100 -d '.\build\ps2-led\ps2-led.hex' -v -rst
```

## 查看数据，判断故障在哪一步

在支持源代码调试的 IDE 中打开 ELF，并在 Watch 中添加 `ps2_led_debug`。
CubeProgrammer 可以按 ELF 符号所对应的地址查看 SRAM；地址以每次编译结果为准，
不能保证改代码后还在同一位置。

| 字段 | 含义 |
|---|---|
| `loops` | 循环计数，应不断增加 |
| `valid_frames` / `invalid_frames` | 有效 / 无效回应计数 |
| `mode` | 最近一次回应的模式字节 |
| `raw` | 最近一次原始回应，常见回中帧 `FF 73 5A FF FF 80 7F 80 7F` |
| `circle_down` | ○ 是否按下，1 为按下 |
| `circle_presses` | 检测到的按下次数，松开或按住时不应连续增加 |
| `led_on` | 用户灯逻辑状态，1 为亮 |

芯片运行时读取原始数组，可能碰到程序正在更新帧；需要一份一致的帧时，
应暂停芯片后再读，查看完恢复运行。

本次实测（2026-09-30）：HEX 下载并校验通过，芯片运行，读到模式 `73`、
标志 `5A` 和回中摇杆数据；多个采样中有效帧计数增加，无效帧计数为 0。
圆圈按下计数有变化，松开后的采样保持不变，用户确认每按一次灯就亮灭切换。
这同时验证了无线配对、四根信号线通信和圆圈键检测。

下载前已将原板内 Flash 只读备份到仓库根目录：
`backups\ps2-live-check-20260930-175621\board-before.bin`。
备份大小 131072 字节，SHA256 为
`35eef9efd53f2659611b3ac7beaa83fd8dd3ed767ba646559c8909a4c229c443`。
备份和构建产物被 Git 忽略。本次只把测试固件烧入板子，未提交或推送代码。
