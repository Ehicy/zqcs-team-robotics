# Dashboard Agent 接口 v1

仅桌面 `dashboard.html`。页面内提供 `window.crtcAgent.status()`（只读副本）和异步
`window.crtcAgent.execute(request)`，不暴露 BLE writer、controller 或原始数据包入口。
这属于同页控制接口，不是可供外网访问的 HTTP 服务；同源脚本仍具有页面权限，复选框不是隔离恶意脚本的安全边界。

浏览器 Agent 可点击顶栏 **Agent 诊断**，在 **Agent JSON 命令** 填写请求，再点击
**执行 Agent 命令**；结果以 JSON 显示。浏览器支持 `navigator.modelContext.registerTool`
时，额外注册 `crtc_dashboard_status` 和 `crtc_dashboard_command` 两个 WebMCP 工具。
不支持 WebMCP 的浏览器仍能使用 JSON 面板和 JavaScript API，不需开启实验性开关。

状态包含 `state`、`canArm`、`blockers[{code,message}]`、反馈年龄（缺失为 null）、
编码器/轮速/舵机数据、当前表单、许可及最多20条 Agent 操作记录。
未连接时只报 `BLE_DISCONNECTED`，不把缺少遥测误报为 UART 硬件故障。
表单值不等于主控已接受值；`state.calibratedMask` 才是主控回报的确认掩码。

请求示例：

```json
{"command":"status"}
{"command":"stop"}
{"command":"led","on":true}
{"command":"configure"}
{"command":"clearFault"}
{"command":"arm"}
{"command":"drive","throttle":1000,"steer":0,"limitMmS":80,"durationMs":300}
{"command":"servo","channel":0,"pulseUs":1500}
```

每行是单独请求；不要一次执行整段。响应为 `{ok,status}` 或 `{ok:false,error:{code,message},status}`。
`led` 成功只代表带响应 BLE 写入完成，不证明光学灯态；`arm` 成功只代表请求提交，
必须再看 `state.armed`，不能把 `state.armPending` 当作解锁完成。
配置和舵机命令等待现有协议匹配会话/序号/轮号的固件 ACK；ACK 不代表实物运动已验收。

- status 和 stop 不需开启许可。其余命令默认禁用，需页面勾选 **允许 Agent 控制**。
- arm/drive 还需单独勾选 **允许 Agent 解锁和短时运动**。所有写命令要求前台可见且有焦点。
- configure 只读取四轮表单，要求四项实测确认均已由操作者核对，不接收远程注入配置，
  不自动勾选、不自动恢复确认。轮径/CPR等默认值不能替代实测。
- configure、clearFault、arm、drive、servo 均要求不足200ms、四轮有效且目标/实测速全零的反馈；
  原有 UART、会话、就绪、故障、ARM 和舵机固件门禁继续有效。
- drive 不自动 ARM，速度上限1–100mm/s、时长50–1000ms；每次短测到期停止锁定。
  运动中拒绝续期/追加命令，stop/status 除外。手动操作、许可撤销、会话变化、断连、
  页面失焦和原有150ms调度中断保护均会终止，不自动续跑。STM32 TTL 仍是最终保护。
- 舵机沿用独立的供电/共地/机构支撑确认，固件必须确认位置型；范围1300–1700μs，
  首次1500μs、后续单步最多25μs。0表示释放输出，可能失去保持力；不是“轴已停稳”。
- 停止锁定、断连、会话改变、失焦和隐藏撤销许可，不保存至 localStorage。
- 首次蓝牙授权仍使用页面连接按钮，不在 API 中绕过设备选择器。

主机测试（不连接实物）：

```powershell
node --test firmware/tests/test_dashboard_agent.mjs firmware/tests/test_web_dashboard.mjs firmware/tests/test_web_remote.mjs firmware/tests/test_web_odometry.mjs firmware/tests/test_competition_field.mjs
```

2026-10-18：上述61项通过；真实 Edge 已通过 JSON 面板读取离线状态。
当前 Edge 未提供 WebMCP，已使用 JSON 入口；点击连接返回“未选择设备”，未建立 BLE 连接，
未发 ARM/DRIVE 或舵机命令，不把主机测试当作实车验收。

PID版：configure 先发送统一 Kp/Ki/Kd，再逐轮机械/速度上限配置（共9个ACK）。往复测试在Dashboard运行，Agent不能与其争抢ARM/DRIVE；STOP始终可用。

2026-10-18：servo通道0=GPIO12大臂、1=GPIO13小臂、2=GPIO14门。0/1仍为1300–1700μs，2为1000–2000μs；0释放、首启1500、每次最多25μs、固件100ms限频不变。角度滑块运行时接口不允许抢占，先STOP取消后再操作。
