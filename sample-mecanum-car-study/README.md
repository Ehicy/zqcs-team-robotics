# 麦克纳姆轮小车：电控新手学习修订版

基于 Serialist/sample-mecanum-car 的 `1fa85bbdcd6c62f33f871284b642959cea975805`，2026-09-15 检查和修改。

**2026-10-02 最新试验源码：保留实测轮位、后轮反向补偿和俯视 O 形麦轮；左杆连续控制平移方向与 PWM 幅度，右杆连续控制旋转，L1/R1 不再调速。上电输出 0、上限 100%，满幅起步为 0.5s 五次 S 曲线。模拟模式松杆和 START 使用短暂弱反转＋短路制动；异常帧立即撤驱动。默认前/后轴横移增益 100/95，旧版 Flash 参数不加载，保留 SELECT 人工辅助学习和停车 Flash 保存。TIM2/TIM3 双输入 PWM 默认 20 kHz。**

**当前已上板：**11220 字节新固件已通过主机测试、Arm GNU 编译、ST-Link 下载校验和独立读回，程序已运行，校准页保持不变。读回时手柄为数字模式0x41并保持停车；用户随后已按MODE／ANALOG，切换后的0x73尚未补读确认。程序按此前 PWM 的 10% 反向输出，名义窗口 30ms；此前输出低于 30% 时只用短路制动。这是开环试验，停车效果待实测。固件、参数和操作见 [S 曲线与急停](docs/S_CURVE_STOP.zh-CN.md)，历史版本见 [本地版本索引](docs/LOCAL_VERSIONS.zh-CN.md)。

**上一版已上板：**左杆25%、右杆50%三次混合映射（[手感调参](docs/PAD_MAPPING.zh-CN.md)），线性时间起步，前100/后95，SELECT单次训练最多5点，Flash记录版本2。10036字节固件已通过主机测试、ST-Link下载校验并独立读回一致；板上模拟模式73、回中使能、前100/后95、八路CCR全0，旧Flash记录未加载。手感和横移改善待实测。历史下载状态见[靠近原始状态的补偿](docs/STRAFE_NEAR_BASELINE.zh-CN.md)。

上一版 9820 字节试验版已通过主机测试、Cortex-M3 编译、ST-Link 下载校验及独立读回；模拟模式73、回中使能、八路CCR为0，横移参数前85/后100。HEX 为 `build/driver-calibration/crtc-driver-calibration.hex`。实车响应和横移学习尚待验证，操作见 [横移学习与保存](docs/DRIVER_CALIBRATION.zh-CN.md)。此前 7260 字节开环版用户实机确认前后正常、横移伴随转弯；历史见 [无编码器 TT 调优说明](docs/MOTOR_TUNING.zh-CN.md)，本轮读取与下载证据见 [ST-Link诊断](docs/STLINK_RESEARCH_2026-10-02.zh-CN.md)。

先打开：
- [检查与修改记录](docs/REVIEW.zh-CN.md)：发现的问题、修复方式、测试范围与剩余限制。
- [电控 B 阅读指引](docs/LEARNING.zh-CN.md)：按四个源码文件逐步学习。
- [测试结果](docs/TEST_RESULTS.txt)。
- Keil 工程：`user/rmcic.uvprojx`，保留标准库与 ARM Compiler 5 配置。

本版主循环在上电/通信异常后等待摇杆回中且按键全部松开，随后才接受控制；无效包清零电机输出。接收器若持续返回格式正确的旧帧，程序仍不能确认无线失联，须实测。

电机驱动 TODO 已完成；`TODO_GRIPPER` 仍是夹爪动作占位。当前接线、编译烧录与验收见 [DRV8833 说明](docs/DRV8833.zh-CN.md)。主机测试运行 `python tests/run_tests.py`，需要 Python 3 和 GCC；Windows 缺少 UBSan 时先设置 `$env:SANITIZE='0'`。

新增独立 GNU 构建入口：`python build_firmware.py --toolchain <Arm GNU bin目录>`，输出 `build/crtc-motor.hex`。原 Keil 工程保留。

以下保留原项目简介和历史材料清单；末尾引脚表已更新为当前 DRV8833 代码的接线。

---

# STM32的简单麦克纳姆轮小车

## 简介

这个是一个由 STM32 最小单片机实现的简单麦克纳姆轮小车，包括了简单的 PWM 波输出和麦轮解算操作。

### 材料

- STM32F103C8T6 单片机最小系统板
- L298N 电机驱动模块
- LM2596 降压模块
- GBJ37-520 电机
- PS2 手柄及其接收器
- 小车底盘
- Keil 5

## 目录

```
├── hardware/           # 代码目录
│   ├── Inc/            # 头文件
│   └── Src/            # 程序文件
│       ├── main.c      # 主程序
│       ├── motor.c     # 底盘控制
│       ├── ps2.c       # PS2 数据接受程序
│       ├── pwm.c       # PWM
│       ├── delay.c     # 软件延时函数
│       └── sys.c       # 系统配置相关
├── lib/                # 硬件库
├── user/               # Keil 5 工程文件
└── README.md           # 项目说明文件
```

## 当前引脚定义（DRV8833 + PS2）

每块 DRV8833 接两只 TT 电机。每只电机的两根线接同一个通道的两个输出，
不把电机的任何一根线固定接 GND。用户已逐键确认：1左后、2右后、3左前、4右前；
正电气输出使两只后轮后退、两只前轮前进，小车程序已补偿后轮极性。

| 模块 | 控制输入与 STM32 引脚 | 电机输出 |
|---|---|---|
| DRV8833① A | AIN1 → PA0，AIN2 → PA1 | AO1、AO2 → 电机1（左后，×） |
| DRV8833① B | BIN1 → PA2，BIN2 → PA3 | BO1、BO2 → 电机2（右后，○） |
| DRV8833② A | AIN1 → PA6，AIN2 → PA7 | AO1、AO2 → 电机3（左前，□） |
| DRV8833② B | BIN1 → PB0，BIN2 → PB1 | BO1、BO2 → 电机4（右前，△） |

本次测试用户选择两节串联 18650 直接给两块模块 VM 供电（标称 7.4V、满电 8.4V），替代此前电机支路 5V；已反馈实机行驶现象，未测马达端电压、电流和温升，不能据此确认 TT 耐压。改接时先移除 VM 原有 5V 正极线，不能将两路正极并接。STM32 核心板 5V 入口和 PS2 转接板仍用稳压 5V；ST-Link 单独供主控时也须与电池负极共地。
每块驱动板接一个标为 GND 的端子即可，电池负极、两块驱动板、STM32 与 PS2 的地必须电气连通。
对本次照片所示模块，STBY 对应使能/休眠，接 STM32 3.3V 保持使能；NC 空脚不接。
当前代码不输出独立 EN 信号，PA8 不负责使能。

| PS2 信号 | STM32 |
|---|---|
| DAT / DI | PB14 |
| CMD / DO | PB15 |
| CS / SEL / ATT | PB12 |
| CLK | PB13 |

小车程序要求手柄模拟模式 `0x73`。上电后松开按键、摇杆回中才使能；
左摇杆控制前后、横移和斜移，方向以车头为准；右摇杆左右控制旋转。
出死区后摇杆幅度连续控制 PWM，L1/R1 已无调速绑定。
START 清空运动目标和按键历史，恢复默认输出上限 100%，保留校准参数；模拟模式启动有限制动，停车结束、松开按键且三个控制轴回中后，再推杆即可行驶。数字/无效帧直接撤驱动。
操作与参数见 [横移学习与保存](docs/DRIVER_CALIBRATION.zh-CN.md) 和 [无编码器 TT 调优](docs/MOTOR_TUNING.zh-CN.md)；轮位及 O 形混合依据见 [O 形麦轮说明与历史记录](docs/JOYSTICK_O.zh-CN.md)。
普通圆圈/叉号只记录松开事件，SELECT 组合键另用于校准；小车入口不驱动舵机。
PC13 用户灯行驶时在任一轮实际 PWM 严格超过 60% 时闪烁，停车时另有保存提示；POWER 电源灯不受程序控制。该灯不检测电流或温度。
独立圆圈键翻灯测试见 [ps2-led 示例](examples/ps2-led/README.zh-CN.md)。
