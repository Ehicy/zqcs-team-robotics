# 四键分别切换四个电机满速启停

本目录当前版本为四键独立控制，每路运行时均为 100% PWM。

- 上电全部停止；先松开所有按键。
- × 切换电机1，○ 切换电机2，□ 切换电机3，△ 切换电机4。
- 每按一次切换对应电机的启停；按住不重复，松开后保持状态，其他电机不受影响。
- 运行时正向 PWM 占空比 100%；实际转向取决于电机接线和安装方向。
- START 全部停止。停止后先松开所有按键再重新启动。
- 摇杆不控制电机。
- 接受 PS2 数字模式 `41` 和模拟模式 `73`。无效帧全部停止，恢复后不自动继续运行。

停止为 DRV8833 的 0/0 滑行，不是主动制动；程序仍无法识别接收器持续返回
格式正确旧帧的无线失联情况。

## 接线

| 按键/通道 | STM32 → 驱动板输入 | 电机两根线 |
|---|---|---|
| × / 电机1 | PA0 → ①AIN1；PA1 → ①AIN2 | ①AO1、AO2 |
| ○ / 电机2 | PA2 → ①BIN1；PA3 → ①BIN2 | ①BO1、BO2 |
| □ / 电机3 | PA6 → ②AIN1；PA7 → ②AIN2 | ②AO1、AO2 |
| △ / 电机4 | PB0 → ②BIN1；PB1 → ②BIN2 | ②BO1、BO2 |

两块驱动板 VM 接电机支路 5V；STBY 为使能，可接 3.3V 或稳压 5V。
GND 与电机电源负极、STM32、PS2 共地；NC 不接。
STBY 改接 5V 时，先断开原来连到 STM32 3V3 的线，不能把 5V 和 3V3 连在一起。
电机两根线分别接同一个 H 桥的两个输出，不把其中一根固定接 GND。

PS2：DAT=PB14、CMD=PB15、CS=PB12、CLK=PB13。

## 编译与下载

在 `sample-mecanum-car-study` 目录运行：

```powershell
python .\build_firmware.py --target motor-buttons --toolchain 'D:\DEVELOP\CQU_AI\tools\arm-gnu-14.3\bin'
& 'D:\programe\stm32\bin\STM32_Programmer_CLI.exe' -c port=SWD mode=UR reset=HWrst freq=100 -d '.\build\motor-buttons\motor-buttons.hex' -v -rst
```

源码入口为 `main.c`，复用仓库 `ps2.c`、`pwm.c`、`delay.c`。
`ps2_read_buttons()` 接受数字/模拟模式；原小车入口仍使用只接受模拟模式的
`ps2_read()`，数字模式的无效摇杆字节不会进入小车运动计算。

## 当前四键独立满速版本验证（2026-09-30）

主机测试全部通过：四个键分别只控制对应一路，运行 CCR=3600、反方向 CCR=0；
按住不重复、松开保持运行、再按对应键仅停止该路、START 优先全停；
模拟/数字模式、无效帧停车及重连先松键均通过。

CubeProgrammer 下载、校验、复位成功；独立读回4204字节与BIN一致，SHA256：
`9161a0ec8dbab4aa312a920b51ad2d920b2c75074b231001f1cad1aff5070be8`。

板上实读：芯片运行，模式 `41`，有效帧计数增加、无效帧为0；armed=1、
motor_mask=0，四路切换计数为0，八个CCR均为0，处于默认全部停止状态。
尚未记录本版四个按键与实际电机位置的对应，待用户逐个按键观察。
日志：`build/motor-buttons/flash-independent-full-log.txt`。
此前○键全开版本的被覆盖区域备份：仓库根目录
`backups/motor-independent-full-20260930-01/board-before.bin`（6144字节）。

## 前一版 ○ 全开测试记录（2026-09-30）

编译通过；主机测试覆盖 ○ 键四路满占空比、按住只切一次、其他三个键无动作、
START 优先停止、无效帧停车及松键后重新使能、数字/模拟模式。

使用已有 CubeProgrammer 2.23.0 / ST-Link 下载、校验并复位成功，独立读回
4132 字节与 BIN 一致，SHA256：
`bc5a2a9d30f954e0470a63548b5d861ddc08e42ed04209ee94f93d2bc7424901`。

板上实读：芯片运行，模式 `73`，有效帧计数增加、无效帧为 0；
○ 键切换计数 3，motor_mask=15，四对 CCR 均为 3600/0，对应 100% 正向输出。
该结果验证控制数据和 PWM 寄存器，不代表已确认实际四个电机的转向或负载能力。
详细日志位于被 Git 忽略的 `build/motor-buttons/flash-all-full-log.txt`。

调试器可查看 `motor_buttons_debug`：`motor_mask` 为 0 表示全停，15 表示全开；
当前独立控制版本使用 `last_motor` 记录最近切换的电机编号，`toggles[4]` 记录四路
各自切换计数。符号地址以每次编译的 ELF 为准。

烧录前的被覆盖区域备份位于仓库根目录
`backups/motor-all-full-20260930-01/board-before.bin`（6144 字节）。
更早的小车完整 C8 范围备份在 `backups/motor-buttons-20260930-01/board-before.bin`。
