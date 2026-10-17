# 下层车固件入口

参赛技术正文已集中至根目录 [docs/lower-car-wiring.md](../docs/lower-car-wiring.md)，包含当前供电、UART、TB6612、编码器分配、构建与灯控验证。

- [参赛文档目录](../docs/README.md)
- [下层开发历程](../docs/02-lower-car.md)
- [四轮电机、编码器与台架入口](../docs/06-lower-motor.md)
- [速度闭环与手机遥控](../docs/08-speed-remote.md)
- [Android BLE 网页面板](web/README.md)
- [四目标半自动方案](../docs/07-semi-auto-plan.md)
- [公开验证与证据](../docs/04-validation.md)

`shared/` 包含双板协议、开环、四轮 PI 与遥控状态机；`esp32/` 是含 BLE 的 PlatformIO 工程，`web/` 是 Android 静态控制面板。STM32 的 `examples/board-link/` 继续禁用电机，独立 `examples/lower-motor/` 已接入 TB6612、原始编码器和 10ms 轮速闭环，输出限幅 25%，手机限速 300mm/s。2026-10-17 已修复并烧入 `lower-motor`，完成通信、灯控和看门狗故障诊断测试，见 [修复记录](../docs/11-lower-watchdog-fix.md)；逐轮架空试转已通过，四轮同时测试触发编码器异常，见 [台架记录](../docs/06-lower-motor.md)；四轮校准、闭环、手机实连和网页发布仍未验收。里程计、导航执行和舵机动作尚未实现。

编码器按用户商品图采用 STM32 3V3 供电，六针线序及方向待实测。13PPR 单相周期、34:1、65mm 轮径仅作为初始换算假设；CPR 暂取 1768/1768/1768/884，四轮默认未确认。STM32 配置仅 RAM，重启需重新确认；网页保存的表单不会自动发送或解锁。测试结论以验证记录为准，UBSan 未运行。
