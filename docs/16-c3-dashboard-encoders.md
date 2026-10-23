# ESP32-C3 Dashboard 与 PA 侧编码器底盘

2026-10-22：上层改回 LuatOS CORE ESP32-C3，手柄输入退出当前构建。
Dashboard 通过 BLE 控制 C3；C3 管三舵机，STM32 管四路 TB6612 电机输出和编码器。
历史 S3、双 STM32 手柄目标及其源码继续保留。

## 已安装与验证

- STM32：`dashboard-motor`，诊断标记 `0xC3D61022`；ST-Link 序列号 `37FF71064E57343655231443`。
- C3：`crtc-c3-direct-pwm-v2`；COM9 / CH343，确认 ESP32-C3 revision v0.4。
- 蓝牙广播：`CRTC-Lower-Car`，服务 UUID `2c4b0001-6f1a-4c9e-9b72-3c06f42b7d11`。
- UART 已握手，`connected=1`；`LED ON` / `LED OFF` 均收到 STM32 ACK。
- 用户已接编码器，四路计数回传；此前 M3/M4 中断过密导致永久锁存的问题已改为自动重试，坏读数不影响手动 PWM。
- 启动和诊断期间 STBY 为低；本轮未执行车辆运动或舵机运动。

固件及诊断记录位于 `output/c3-dashboard-20261022/`。烧写前原固件的备份保存在操作者本地工作区。

## 接线

接线图：`firmware/web/esp32c3-wiring.html`，提供完整排针、独立串口图和逐根端点表。
默认 C3 正面朝人、USB/RST在上、天线朝下；STM32 USB朝上。

| 功能 | C3 | 底盘 STM32 |
|---|---|---|
| C3发送 → STM32接收 | IO18，右排第12针 | PB11，右排第5针 |
| STM32发送 → C3接收 | IO19，右排第11针 | PB10，右排第6针 |
| UART参数 | UART1，115200 / 8N1 | USART3，115200 / 8N1 |

GPIO号优先认丝印；排针序号仅适用于上述朝向。两板地线按图分别落在降压器 OUT−。
COM9 已确认经典 USB 转串口款，原生 USB 的 GPIO18/19在当前固件中让给 UART1。

| 轮位 | 编码器 A | 编码器 B | 计数方式 |
|---|---|---|---|
| M1右前 | PB4，左排12 | PB5，左排13 | TIM3 部分重映射，4倍频 |
| M2左前 | PB6，左排14 | PB7，左排15 | TIM4，4倍频 |
| M3右后 | PB0，右排8 | PB1，右排7 | EXTI0，仅A相双边沿，2倍频 |
| M4左后 | PB8，左排16 | PB9，左排17 | EXTI8，仅A相双边沿，2倍频 |

四路编码器 VCC 沿用历史 3.3V 接法，分别落在底盘右排第3针3V3；各编码器 GND 落在右排第1针 GND。
用户确认13PPR霍尔，实际编码器额定供电尚待核对；A/B必须为3.3V兼容信号，PB0/PB1不可直接接5V输出。
六线插头的孔序沿用旧资料，识别电源、A/B时仍以实际线束定义为准。

电机 PWM 保留 PA0/PA1/PA2/PA3，方向保留 PA4/5、PA6/7、PA8/9、PA10/15；PB15 为共用 STBY。
SWD 保留 PA13/PA14。TIM2不再用作编码器，避免与PWM冲突。
大臂/小臂/门信号分别是 C3 IO7 / IO10 / IO0。

## Dashboard直接控制

打开 `http://127.0.0.1:8080/dashboard.html?v=c3-pwm3`，连接蓝牙设备 `CRTC-Lower-Car`；已授权设备可直接重连。
默认“直接 PWM”：按住方向直接输出，滑块为0–100%占空比，初值20%。不需要ARM、PID初始化、校准确认或清故障；电机/底盘反馈异常不联停其他电机。每轮独立显示实测速度，异常样本丢弃且不参与控制；PWM模式没有目标速度，目标栏显示“—”。
可选“PID速度控制”保留原速度环与参数流程；普通行驶的PWM和PID往复测试不共用物理单位。切换方式会停止当前动作。PID首次方向输入仍自动应用表单并取得控制回执。
连接本身不启动车辆；STOP、断连、会话变化、页面失焦和指令过期撤销当前动作，迟到回执不恢复运动。
机械参数标记表示选用的配置已应用，不代表已实测；每圈计数与方向仍应按实际编码器核对。

三张卡已分为独立布局：1统一PID、2四轮机械参数、3往复测试，手机端单列。
根因是三张卡此前都使用同一个CSS `grid-area: calibration`，导致重叠；此修复不改变PID算法。

## 反馈与PID

STM32每10ms按实际间隔采样轮速，四轮有各自的积分、D滤波和输出历史，共用一组从C3下发的Kp/Ki/Kd。
仅回传原始计数、实测/目标轮速及状态，不回传PID内部积分历史。
M1/M2和M3/M4倍率不同。13PPR为单相周期、34:1减速比的旧假设对应1768/1768/884/884计数/轮；Dashboard已使用独立缓存键，避免复用旧三硬件计数方案的M3参数。
每轮手转输出轴一整圈所得差值才是本车的实测CPR。

软件采样有效标志不等于识别到了编码器插头；静止的有效计数0不能证明线路接好。
中断计数保留20μs脉宽资格判断，每路每毫秒最多32次中断；中断过密只暂停该路采样至下一窗口后自动重试，不关闭手动PWM，也不需要清故障。独立测速对不合理跳变丢弃该轮样本，其他轮继续显示。

## 构建

STM32：

```powershell
python sample-mecanum-car-study/build_firmware.py --target dashboard-motor --toolchain D:/DEVELOP/CQU_AI/tools/arm-gnu-14.3/bin
```

C3（英文临时路径构建，避免旧工具链中文路径问题）：

```powershell
python firmware/tools/build_esp32_ascii.py --env c3-dashboard --pio D:/DEVELOP/CQU_AI/tools/ps2-build-venv/Scripts/pio.exe --core-dir D:/DEVELOP/CQU_AI/tools/platformio-ps2-core --output output/c3-dashboard-20261022/esp32
```

C3首次完整写入：`firmware/tools/flash_c3_dashboard.ps1 -Port COM9`。
与 S3 的 APP 更新脚本分开；C3 bootloader 地址为0x0，不使用S3镜像或熔丝操作。
USB下载时先断开C3外部+5V分支，保留GND，避免与USB供电互灌；舵机动力单独断开。

## 验证范围

主机协议、四轮速度环、编码器脉冲资格判断测试通过；本轮网页55项测试通过（修正原保留位测试以接受新增PWM能力位）。新增C3串口双端模拟覆盖无ARM/PID/校准下100%PWM、坏编码器不影响指令、轮方向、STOP后直接再按、命令过期及逐轮测速恢复。
新增 PA 资源映射测试覆盖 GPIO、定时器、UART与SWD互不冲突、双端CPR初值一致、启动电机/舵机无输出。
本机未运行UBSan。模拟收敛和编译结果不等于当前电机接线、PID参数与机械端点实测完成。

C3/PA 资源与串口双端模拟可用 `python firmware/tests/run_c3_tests.py` 复现；Windows 无 UBSan 运行库时先设置 `SANITIZE=0`。网页测试入口为 `npm run test:web`。
