# 下层车固件入口

参赛技术正文已集中至根目录 [docs/lower-car-wiring.md](../docs/lower-car-wiring.md)，包含当前供电、UART、TB6612、编码器分配、构建与灯控验证。

- [参赛文档目录](../docs/README.md)
- [下层开发历程](../docs/02-lower-car.md)
- [公开验证与证据](../docs/04-validation.md)

`shared/` 是双板共用协议；`esp32/` 是 PlatformIO 工程；STM32 入口在 `sample-mecanum-car-study/examples/board-link/`。两端 `board-link` 固件保持电机输出禁止；真实编码器采样、BLE 与导航尚未实现。
