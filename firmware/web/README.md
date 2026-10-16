# 下层车 Android BLE 控制面板

**PS2 构建说明：**ESP32 使用 `ps2-chassis` 时，手机仅做停车校准、状态、灯控和 STOP；ARM/DRIVE 不会被执行，实际行驶由 PS2 解锁/摇杆控制。下文手机行驶流程适用于原 `board-link` ESP32 环境。当前 PS2 接线和操作见 [说明](../../docs/09-ps2-chassis-pid.md)。

静态页面，无构建依赖。浏览器端已实现连接、明确 ARM 解锁、按住行驶/松开归零、停止锁定、灯控、状态和四轮编码器/轮速显示，以及逐包确认的四轮参数配置。本次只编写与测试软件，未连接真实 BLE 设备、烧录或发布网站；效果仍需对应新固件实板验收。

本目录有**两个**面板，共用 `protocol.js` 的同一套线协议与安全状态机：

| 文件 | 适用 | 控制方式 |
|---|---|---|
| `index.html` + `app.js` | 手机（Android Chrome，HTTPS） | 屏幕方向按钮 |
| `dashboard.html` + `dashboard.js` | 电脑（桌面 Chrome/Edge，`localhost`） | **键盘 WASD** |

## 桌面 WASD 遥控台（dashboard）

一键启动：双击备赛目录下的 `打开BLE遥控台.bat`，它会起 `127.0.0.1:8080` 静态服务并用 Chrome/Edge 打开
`http://localhost:8080/dashboard.html`（`localhost` 属于安全上下文，Web Bluetooth 可用）。

也可以手动启动：

```powershell
python -m http.server 8080 --bind 127.0.0.1 --directory firmware/web
# 浏览器访问 http://localhost:8080/dashboard.html
```

操作顺序与手机面板一致，只是把按住方向键换成按住键盘：

1. 点“连接蓝牙”，在浏览器弹窗里选 `CRTC-Lower-Car`。
2. 保持锁定，勾选四个“已实测核对”后点“停车后写入四轮配置”，等每轮依次 ACK。
3. 点“解锁控制 ARM”，主控确认后徽标变为“已解锁”。
4. 按住 <kbd>W</kbd><kbd>A</kbd><kbd>S</kbd><kbd>D</kbd> 行驶；方向键等效。`W`/`S` 是前后，`A`/`D` 是左右转向，组合即为斜向；松开全部按键立即发零 DRIVE。
5. <kbd>空格</kbd> 或 <kbd>Esc</kbd> 立即停止并锁定。

界面同时给出：当前意图、按固件混控公式算出的预计左右轮速、主控状态标志（链路/就绪/已确认轮数）、
四轮原始计数与实测/目标轮速条、逐块板载灯控制、清故障，以及带时间戳的事件日志。

键盘相关的安全约定（都已在自动化测试里锁定）：

- 未解锁时按键只显示意图，不产生任何 DRIVE；
- 解锁的上升沿会清空按住的键，必须**重新按下**才动，避免“按住 W 再点 ARM”直接窜车；
- 焦点在输入框/下拉框内时按键不驱动底盘，方向键与空格不会滚动页面；
- 页面失焦、隐藏、`pagehide`、蓝牙断连、事件循环间隔 ≥150ms 都会停止并锁定；
- 主控状态超过 500ms 未更新、或底盘故障/未就绪，自动停止并锁定。

**固件要求：**BLE 的 ARM/DRIVE 只在 ESP32 `board-link` 环境（`CRTC_USE_PS2=0`）下被接受；
`ps2-chassis` / `ps2-direct` 构建里 BLE 只做校准、状态、灯和 STOP，行驶由 PS2 手柄负责，
此时 dashboard 可以连接和看状态，但按 WASD 不会让车动。STM32 侧仍需烧 `lower-motor` 目标，
否则主控不允许驱动。

## 打开页面

Android 手机使用支持 Web Bluetooth 的 Chrome，在 **HTTPS** 地址打开 `index.html`。普通手机局域网 `http://电脑IP:端口` 不属于安全上下文，不能连接蓝牙。`localhost` 只指浏览器所在设备自身，不代表局域网电脑；不要通过关闭浏览器安全设置绕过这一要求。

电脑仅预览布局可在仓库根目录启动：

```powershell
python -m http.server 8080 --bind 127.0.0.1 --directory firmware/web
```

然后访问 `http://localhost:8080`。页面会检查安全上下文和 Web Bluetooth 能力；不支持时显示原因并禁用连接。静态文件可整体放入团队选定的 HTTPS 站点，但本任务未部署或发布。

## 控制流程

1. 连接设备 `CRTC-Lower-Car`。新连接、新会话均锁定，不恢复上次的动作。
2. 保持锁定，手转各轮查看原始计数；在校准表填写方向、每轮每圈计数、轮径和参数。默认“实测确认”不勾选，必须实际核对后确认。
3. 车轮停稳后，点击写入四轮配置。每轮依次发送机械参数和速度环参数，等待匹配会话、源序号、轮号的专用 ACK 后再继续；拒绝、超时或停止时中断。配置只保存在 STM32 RAM，断电后需要重新写入。
4. UART 在线、四轮已确认、速度闭环就绪且无故障时，手动点击 ARM；只有主状态确认 armed，方向键才可用。
5. 默认速度上限 100mm/s，可调 0–300mm/s。按住前后或左右转向，四胶轮底盘不提供横移。松开方向发零 DRIVE，保留解锁；大 STOP 立即清除待发任务并锁定。

页面隐藏、失去焦点、触控取消/捕获丢失、断连、会话改变或事件循环间隔达到 150ms 均清除触控并停止锁定。回来后不会自动续跑。发送端只允许一个写操作在途，待发 DRIVE 仅保留最新采样，100ms 过期丢弃；已交给蓝牙栈的写无法撤回，STOP 会在其结束后优先发送。车辆端仍必须执行自己的 TTL 和断联保护，不能依赖浏览器必定成功发送 STOP。

固件接受 STOP 后会更新会话，因此正常的新会话只触发本地清队列和锁定，不再回发 STOP，避免循环。只有状态异常地报告未经本页确认的 armed 时才补发一次 STOP。ARM 自身不产生运动；页面等到主状态确认后才开始发送 DRIVE，首条 DRIVE 才开始运动有效期。

“记住本机表单”只写当前浏览器 localStorage，不向车辆发送配置。重新加载会清除实测确认勾选，也不会自动 ARM 或移动。Kp/Ki 是 `milli` 整数形式；默认 1000/2000、最大轮速 300 和 ESP32 下发的加速度 500mm/s² 都是待实测参数。默认 CPR 1768/1768/1768/884 依据 **13PPR 为单相周期、34:1 减速比** 的假设计算，前三路 4×、第四路 2×；65mm 轮径也需实测。

参数范围：轮径 0.1–500.0mm；CPR 1–1000000；Kp/Ki 各 0–20000 且不能同时为零；最大轮速 1–300mm/s。配置被拒绝可能是车轮仍在转动、故障或主控忙，排除原因并停稳后再手动重试。页面显示的 active 仅代表控制状态，不是独立运动检测。

## 协议与文件

服务 UUID：`2c4b0001-6f1a-4c9e-9b72-3c06f42b7d11`；控制与状态特征将 `0001` 分别替换为 `0002`、`0003`。控制采用 **write-with-response**，状态采用 read/notify，固定 20 字节、小端、版本 1。旧灯控/开环固件不提供本页面所需的速度闭环 BLE 能力。

- `protocol.js`：纯协议、状态解码、单在途写队列、ACK 匹配、会话与解锁状态机。
- `app.js`：Web Bluetooth、触控生命周期与 DOM 适配；不会在加载页面时主动连接设备。
- `index.html`、`style.css`：移动端布局及校准表。
- `dashboard-input.js`：WASD 按键折叠、混控预览、意图描述等纯逻辑，不碰 DOM。
- `dashboard.js`、`dashboard.html`、`dashboard.css`：桌面键盘面板。
- `../tests/test_web_remote.mjs`：手机面板的 Node 内置测试，无额外包依赖。
- `../tests/test_web_dashboard.mjs`：遥控台纯逻辑与“按键→意图→报文→安全门禁”链路测试。
- `../tests/test_dashboard_page.py`：Playwright 真实浏览器验收（假 BLE 外设 + 真键盘事件 + 写出的报文断言）。

从仓库根目录执行：

```powershell
node firmware/tests/test_web_remote.mjs
```

自动测试覆盖编码字段与上下界、状态解析、STOP 优先、旧运动采样替换/过期、会话与断连隔离、ARM 确认、150ms 调度中断，以及配置 ACK 匹配与失败中止。自动测试不等于 Android 蓝牙兼容性、实测轮速或电机安全验收。

2026-10-16 本次本地复验：15 组 Node 测试通过，`app.js` 与 `protocol.js` 语法检查通过。包括 STOP 后会话更新不重复发送 STOP、普通灯控与运动消息保持源序号顺序的回归检查。未操作真实蓝牙设备。

## 来源

页面与协议适配为本项目代码，不依赖前端框架。底层 STM32/ESP32 工程和第三方来源见仓库 `docs/05-open-source.md`。浏览器平台依据为 [Chrome Web Bluetooth 文档](https://developer.chrome.com/docs/capabilities/bluetooth)；页面不使用经典蓝牙 SPP。
