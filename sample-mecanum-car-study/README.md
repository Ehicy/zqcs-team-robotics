# 麦克纳姆轮小车：电控新手学习修订版

基于 Serialist/sample-mecanum-car 的 `1fa85bbdcd6c62f33f871284b642959cea975805`，2026-09-15 检查和修改。

**2026-09-24：已适配两块 DRV8833 控制四个电机，TIM2/TIM3 双输入 PWM，默认 20 kHz。实际轮位、方向、电流和遥控失联行为仍需实测。**

先打开：
- [检查与修改记录](docs/REVIEW.zh-CN.md)：发现的问题、修复方式、测试范围与剩余限制。
- [电控 B 阅读指引](docs/LEARNING.zh-CN.md)：按四个源码文件逐步学习。
- [测试结果](docs/TEST_RESULTS.txt)。
- Keil 工程：`user/rmcic.uvprojx`，保留标准库与 ARM Compiler 5 配置。

本版主循环在上电/通信异常后等待摇杆回中且按键全部松开，随后才接受控制；无效包清零电机输出。接收器若持续返回格式正确的旧帧，程序仍不能确认无线失联，须实测。

电机驱动 TODO 已完成；`TODO_GRIPPER` 仍是夹爪动作占位。当前接线、编译烧录与验收见 [DRV8833 说明](docs/DRV8833.zh-CN.md)。主机测试运行 `python tests/run_tests.py`，需要 Python 3 和 GCC；Windows 缺少 UBSan 时先设置 `$env:SANITIZE='0'`。

新增独立 GNU 构建入口：`python build_firmware.py --toolchain <Arm GNU bin目录>`，输出 `build/crtc-motor.hex`。原 Keil 工程保留。

以下保留原项目说明，材料清单和引脚描述属于上游硬件：

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

##  引脚定义

使用了PORTA PORTB

- motor PWM引脚定义

1 -> PA6  
2 -> PA7  
3 -> PB0  
4 -> PB1  

- motor逻辑引脚定义

1 -> PA8 PA9  
2 -> PA10 PA11  
3 -> PA5 PA4  
4 -> PA3 PA2  

- ps2数据引脚定义

DATA -> PB12  
CMD -> PB13  
CS -> PB14  
CLK -> PB15  
