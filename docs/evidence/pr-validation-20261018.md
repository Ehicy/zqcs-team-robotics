# 2026-10-18 发布前主机复测

基线：上游main/d0bde24；发布分支codex/20261018-pid-usb-servo3-dashboard。

- `npm ci --ignore-scripts`：锁定仓库内测试依赖，不依赖工作区外临时node_modules。
- `npm run test:web`：72项网页逻辑测试、桌面与手机两页真实模块离线DOM启动通过。
- `npm run preview:wiring`：11项接线净表/编辑器/原样式基准/路由及渲染测试通过；20模块、82线。
- Python/GCC：PID、速度环、遥控、桥接、板间协议、电机控制、编码器、USB更新8套通过；上层原车回归通过。
- USB更新核含516个Flash操作失败注入；生产DFU状态机与封包校验通过。
- 主机C复测显式`SANITIZE=0`，UBSan未运行；不将这些主机测试记为实际运动验收。
- 本次PR准备没有连接/复位/烧录硬件，没有发送运动或舵机指令；此前硬件USB、偏移APP启动与双板通信证据另见`usb-servo3-20261018.json`。
- 原始Flash备份保持仓库外，PR仅包含源码、说明、可用固件产物、图像和验证记录。

复现：Node22+、Python3、GCC；仓库根安装npm依赖后执行上述命令。C测试入口在`firmware/tests/run_*_tests.py`（协议套件为`run_tests.py`），上层入口为`sample-mecanum-car-study/tests/run_tests.py`。构建需要Arm GNU与PlatformIO，文档中另列安装路径示例。
