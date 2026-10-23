# 历史双 STM32 应急配置

当前比赛入口为 [ESP32-C3 Dashboard 直控](../../docs/16-c3-dashboard-encoders.md)。本目录保留双 F103 和后轮点动的可复现源码，不参与 `c3-dashboard` 或 `dashboard-motor` 构建。

此目录是独立双 F103 配置：`fallback-remote`＋`fallback-motor`，**不链接**编码器、PID、ESP32/BLE 或 Dashboard。旧实现保留在原位置，旧构建目标仍可编译。

- `fallback_control.c`：整车 LOCKED/DRIVE/SERVO/TUNE、PS2 混控、限位、组合键和调参。
- `fallback_motor.c`：五次曲线起步、换向等待、一次性限时反推、故障撤输出。
- `fallback_wire.c`：独立会话与消息类型，20ms 指令/50ms 状态/120ms 输入期限。
- `fallback_runtime.c`：两块板实际调用的输入路由和停止事件处理；整链路主机测试复用同一实现，不另造一套控制逻辑。
- `fallback_store.c`：A 板最后两页的参数日志，显式保存。
- `fallback_pins.h`：B 板 GPIOA 电机信号及 PB UART/STBY 分配；与旧 PID 引脚不同。

引脚资源、状态、边界、协议、Flash 断电模型测试入口：`firmware/tests/run_fallback_tests.py`。此前底盘 B 和上方主控 A 已 ST-Link 直烧并校验；当前已迁移到 C3，硬件安装状态以当前方案说明为准。
