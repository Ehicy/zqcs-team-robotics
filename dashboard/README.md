# Dashboard 便携版

[下载 CRTC-Dashboard-20261024.zip](CRTC-Dashboard-20261024.zip?raw=true)

1. 在 Windows 10/11 解压整个 ZIP，打开电脑蓝牙。
2. 双击 `Start-Dashboard.cmd`，Edge 会打开 Dashboard。保留启动器窗口，无需安装 Python、Node 或开发环境。
3. 点击“连接蓝牙”，首次选择 **CRTC-Lower-Car-146393F8F58E**。旧电脑先点“暂停重连 / 交给另一台电脑”并关闭网页。

默认前进/后退 **19.1%**，独立转向 **21.8%**；滑块均可调至 100%。

| 功能键 | 大臂 | 小臂 |
| --- | --- | --- |
| F / 功能1 | 232°，0秒 | 150°，0.25秒 |
| G / 功能2 | 115°，0秒 | 86°，0秒 |
| H / 功能3 | 不动作 | 147°，0秒 |

时间均相对于本次按键时刻。不同功能键保留2秒间隔。新浏览器自动使用这些默认动作；已有自定义配置保留，点击“恢复本套 F/G/H 默认参数（只保存）”才更新。打开网页与恢复默认均不执行动作。原有路线、动作可导出/导入 JSON。

连接后从 C3 Flash 读取轮参数、PID 和舵机范围，不会把网页初值自动写回。当前车的 C3 `power-switch-v14` 要求底盘停稳后操作舵机；同时操作需要支持并行能力的 C3 固件，仅更新网页不能解除固件互斥。本包不包含固件，也不烧录。

固定使用 `http://127.0.0.1:8080/dashboard.html`，不同来源的蓝牙授权与浏览器记录独立。服务器仅在本机运行，使用时无需联网；需要支持蓝牙的电脑与 Edge/Chrome。8080 端口若被其他服务占用，启动器会提示退出。

从源码重新打包：`python firmware/tools/build_dashboard_portable.py --output <新的输出目录>`。开发验证使用 Node 的 `--test firmware/tests/*.mjs`、`--experimental-vm-modules firmware/tests/test_web_boot.cjs`（DOM 测试依赖 linkedom），以及 `python firmware/tests/test_portable_dashboard.py <解压后的目录>`。
