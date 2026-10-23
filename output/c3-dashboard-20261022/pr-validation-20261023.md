# PR 复测记录 · 2026-10-23

基于上游 main `c608684` 的独立 checkout，汇总 2026-10-22 的 C3、底盘、接线图及 Dashboard 改动。历史应急目标的源文件作为构建入口和 PA 引脚依赖一起保留。

- 网页86项 Node 测试通过；Dashboard 与手机页的真实 ES 模块离线 DOM 启动通过。离线加载器已适配用于刷新浏览器缓存的模块查询参数。
- `run_c3_tests.py`：PA 引脚资源、CPR 双端一致、无 ARM/PID/校准的满量程 PWM、错误编码器不阻止手动输入、方向和停止恢复、逐轮测速测试通过。
- 协议、遥控、桥接、速度环、电机、编码器、应急固件八套 Python/GCC 主机入口通过；应急状态机486971个断言通过。原麦轮应用的主机测试通过。
- STM32 `dashboard-motor`、`fallback-motor`、`fallback-remote` 用 Arm GNU 14.3 编译通过。
- ESP32-C3 `c3-dashboard` 用既定 PlatformIO/Arduino 环境从此 checkout 重建通过。
- C3 接线图18个模块、72条线的端点全部可解析，照片资产齐全。
- `git diff --check` 通过。

Windows 主机使用 `SANITIZE=0`，未运行 UBSan。此次 PR 整理没有连接、烧录、复位或驱动实物；实板写入和链路记录来自10月22日。
