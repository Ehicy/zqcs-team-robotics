# 下层车固件入口

当前烧录状态（2026-10-17）：STM32 闭环 PI 与 HAL 的 PWM 上限为 50%，开环仍为 25%，已烧录并独立读回一致。随后网页四轮 300mm/s 参数已全部 ACK，用户确认直行明显加速，尚未验收实际最高轮速和带载能力。ESP32 的转向修正已在 `crtc-lower-turn-v3` 验证，随后更新为 `crtc-lower-servo-v1`（上传哈希及版本核对通过，舵机版未独立读回），左侧 M2/M4、右侧 M1/M3；烧录、应用区读回和版本核对通过，UART 正常。A/左键原地左转、D/右键原地右转，首次用 100mm/s 核对，用户已确认左右转动并停车、无故障；长期转向稳定性待验收。低目标速度不保证低瞬时占空比。软件测试通过，UBSan 未运行；见 [STM32 证据](../docs/evidence/pwm50-flash-20261017.json)与 [ESP32 转向证据](../docs/evidence/turn-v3-flash-20261017.json)。

参赛技术正文已集中至根目录 [docs/lower-car-wiring.md](../docs/lower-car-wiring.md)，包含当前供电、UART、TB6612、编码器分配、构建与灯控验证。

- [参赛文档目录](../docs/README.md)
- [下层开发历程](../docs/02-lower-car.md)
- [四轮电机、编码器与台架入口](../docs/06-lower-motor.md)
- [速度闭环与手机遥控](../docs/08-speed-remote.md)
- [Android BLE 网页面板](web/README.md)
- [四目标半自动方案](../docs/07-semi-auto-plan.md)
- [公开验证与证据](../docs/04-validation.md)

`shared/` 包含双板协议、开环、四轮 PI 与遥控状态机；`esp32/` 是含 BLE 的 PlatformIO 工程，`web/` 是 Android 静态控制面板。STM32 的 `examples/board-link/` 继续禁用电机，独立 `examples/lower-motor/` 已接入 TB6612、原始编码器和 10ms 轮速闭环，开环限幅 25%、闭环限幅 50%，手机限速 300mm/s。2026-10-17 已修复并烧入 `lower-motor`，完成通信、灯控和看门狗故障诊断测试，见 [修复记录](../docs/11-lower-watchdog-fix.md)；逐轮架空试转已通过，四轮同时测试触发编码器异常，见 [台架记录](../docs/06-lower-motor.md)；四轮校准、闭环、手机实连和网页发布仍未验收。导航执行尚未实现；ESP32 双舵机手动微调与网页面板已实现并烧录，DS3128 小范围转回有目视确认，MG995 回执通过、目视待确认，详见 [舵机使用说明](web/README.md#双舵机手动微调)。

编码器按用户商品图采用 STM32 3V3 供电，六针线序及方向待实测。13PPR 单相周期、34:1、65mm 轮径仅作为初始换算假设；CPR 暂取 1768/1768/1768/884，四轮默认未确认。STM32 配置仅 RAM，重启需重新确认；网页保存的表单不会自动发送或解锁。测试结论以验证记录为准，UBSan 未运行。
