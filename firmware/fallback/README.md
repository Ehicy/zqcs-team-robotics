# 当前应急配置

此目录是独立双 F103 配置：`fallback-remote`＋`fallback-motor`，**不链接**编码器、PID、ESP32/BLE 或 Dashboard。旧实现保留在原位置，旧构建目标仍可编译。

- `fallback_control.c`：整车 LOCKED/DRIVE/SERVO/TUNE、PS2 混控、限位、组合键和调参。
- `fallback_motor.c`：五次曲线起步、换向等待、一次性限时反推、故障撤输出。
- `fallback_wire.c`：独立会话与消息类型，20ms 指令/50ms 状态/120ms 输入期限。
- `fallback_runtime.c`：两块板实际调用的输入路由和停止事件处理；整链路主机测试复用同一实现，不另造一套控制逻辑。
- `fallback_store.c`：A 板最后两页的参数日志，显式保存。
- `fallback_pins.h`：B 板 GPIOA 电机信号及 PB UART/STBY 分配；与旧 PID 引脚不同。

操作与全部参数：[应急指南](../../docs/14-dual-stm32-fallback.md)。引脚资源、状态、边界、协议、Flash 断电模型测试入口：`firmware/tests/run_fallback_tests.py`。底盘 B 已 ST-Link 直烧并校验，上方主控 A 也已直烧并校验；底盘已按用户要求再次重烧并校验；没有实车验收，主机测试不能替代电机和机构验收。
