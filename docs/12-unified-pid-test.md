# 统一 PID 与 Dashboard 往复测试

2026-10-18，根据用户确认实现。本文描述新版源码；本轮没有烧录、复位或驱动实物。
用户已反馈驱动板、电池和升压器通过带载验证；本轮不把该反馈写成代理自行执行的验收。

## 分层与状态机

```text
Dashboard：手动模式 / 有限往复测试模式、统一PID设置、反馈显示
  → BLE：常规控制命令和状态
ESP32：始终运行常规桥接、输入校验、参数初始化、舵机及遥测固件
  → UART：目标轮速、统一PID系数、机械参数
STM32：同一约10ms节拍，依次计算四轮独立PID → TB6612
```

往复程序位于 `firmware/web/pid-test.js`，不在ESP32固件中。不需要为测试换一份ESP程序。
STM32四轮各自保存积分、上次实测速度、微分滤波历史和反转时序，不共享这些运行状态。
PID中间P/I/D量不回传；保留原有计数、轮速、故障、状态和参数ACK遥测。

`chassis_state.h/c`定义整车状态：等待链路、等待PID初始化、等待四轮校准、锁定、已解锁、运行、故障。
ESP32所有BLE事件由常规owner循环消费；UART参数事务异步等待ACK，不阻塞通信循环。
ARM必须处于锁定且就绪状态，DRIVE只允许已解锁/运行；故障、断联、过期撤销运动许可。
STM32根据链路、PID/机械配置和故障状态门控输出，旧25%开环诊断保持独立能力边界。

## PID 算法与初始化

- 系数统一初始化为 `Kp_milli=1000, Ki_milli=2000, Kd_milli=0`，1000表示1.000。
- `u=Kp*error + Ki*∫error dt - Kd*filtered(d measuredSpeed/dt)`。
  使用实际采样间隔；D对实测速度求导，避免目标突变产生微分冲击；一阶低通时间常数30ms。
- 定点整数、积分抗饱和、目标加速度斜坡保留。输出不能在目标尚未过零时反向反推；换向至少滑行50ms。
- STOP、故障和改参数清I/D状态，四轮PID在同一节拍独立顺序执行，不是四个异步任务。
- ESP32启动/板间新会话时下发统一系数；STM32 `pid_initialized=0` 时闭环不允许驱动。
  收到全局参数且停稳校验通过后一次性更新四轮，成功ACK才解除PID初始化门禁。
- 只保存RAM。ESP32重启恢复统一默认值；仅STM32重启时，ESP会重发其当前RAM系数。
  机械参数的实测确认不从ESP缓存自动恢复；重新配置后还需显式解锁。

## Dashboard 操作

1. 成套更新新版STM32、ESP32固件，再刷新网页；连接后等待PID初始化。
2. 核对四轮方向、CPR和轮径，勾选真实实测确认。在“统一PID参数”输入三个系数。
3. “停车后写入四轮配置”先发统一PID，再逐轮发机械和最高速度，共9个匹配ACK。
   只改PID可用独立按钮，不改变机械标定或自动解锁。
4. 往复测试设置单程50–3000mm、1–20次、速度比例1–100%，默认300mm、2次、100%。
   满速指四轮最高目标设置中的最小值；默认最高目标仍是300mm/s，可按需求改至1000mm/s。
5. 确认车辆落地、前后空间及停车余量和物理断电后，按“解锁并开始往复测试”。
   流程是前进、停稳至少400ms、后退、停稳；全部完成停止并锁定。

行程由四轮连续编码器计数和本次已ACK机械标定计算，前后两段分别建立基准。
接近端点降低目标上限，仍不能保证机械瞬停/精确定位；打滑和滑行会影响实际位置。
结果显示每段编码器行程和端点偏差，不将端点超程误称为轮速PID超调，也不自动拟合“最优PID”。
下方折叠说明包含P/I/D调整顺序、振荡/持续误差/噪声/饱和/左右不一致等现象的排查。

按STOP、手动方向接管、手动改速、失焦、隐藏、断联、会话变化、反馈过期、计数跳变、
轮间行程差过大、无行程进展或单段超时均中止，不自动恢复。Agent不能与用户往复测试争抢运动，STOP仍可用。
刷新网页不会自动跑测试，也不会将浏览器缓存当成已应用配置。

## 额定值与范围

MG370额定参考260rpm、65mm轮约885mm/s；该数值只作参考，不是软件禁止越过的阈值。
当前目标范围0–1000mm/s，最高轮速配置1–1000mm/s；允许明确确认后的超额定测试。
闭环PWM硬上限由50%提高至100%，开环诊断仍为25%，TIM4 PWM频率保持20kHz。
系数范围0–20000 milli，Kp/Ki不可同时为0；保护边界不因允许超额定而取消。
满目标不保证达到对应实测轮速，短行程也不一定达到速度上限；长期饱和不能靠加积分强推。
PWM限幅不是电流限制。电源为用户现用的2S统一先升12V再降5V方案。

## 协议与兼容

- BLE固定20字节，新增操作码9：统一PID，12/13字节保留0，14/16/18分别为LE u16 Kp/Ki/Kd。
- UART新增类型13，载荷6字节：三个LE u16系数。配置ACK保留相同事务匹配，轮号253表示全局PID。
- 原逐轮20字节校准载荷保留。逐轮速度上限事务不得改出不同PID；STM32逐轮机械校准保留现有统一系数。
- BLE主状态flags bit5表示PID初始化已确认；byte3高半字节为整车phase，低半字节保留原reason。
- 老STM32不会ACK新全局PID，ESP32保持未就绪；新STM32配老ESP32不会自动开放闭环。
  老网页不理解新flags，不能拿旧网页的显示当新版验收，应同时更新网页。

## 构建与测试

```powershell
# 本机GCC缺UBSan运行库，因此这些运行需明确记录UBSan未执行。
$env:SANITIZE='0'
python firmware/tests/run_pid_tests.py
python firmware/tests/run_speed_tests.py
python firmware/tests/run_remote_tests.py
python firmware/tests/run_bridge_tests.py
python firmware/tests/run_tests.py
python firmware/tests/run_motor_tests.py
python firmware/tests/run_encoder_tests.py
node --test --test-timeout=15000 firmware/tests/test_*.mjs
node --experimental-vm-modules firmware/tests/test_web_boot.cjs
python sample-mecanum-car-study/build_firmware.py --target lower-motor --toolchain D:/DEVELOP/CQU_AI/tools/arm-gnu-14.3/bin
python firmware/tools/build_esp32_ascii.py --pio D:/DEVELOP/CQU_AI/tools/ps2-build-venv/Scripts/platformio.exe --core-dir D:/DEVELOP/CQU_AI/tools/platformio-ps2-core
```

测试包含生产PID独立滤波/无目标冲击/统一原子配置、真实协议初始化/ACK、失败锁定与恢复、整车状态，
以及有限往复/手动接管边界、反馈和调度失效、超额定确认、真实两页ES模块离线DOM启动。
离线DOM不是浏览器响应式验收，也不是实车验收。固件构建脚本不包含自动上传。

新产物位于 `output/pid-20261018/`。烧录前须隔离VM、核对唯一供电与USB回灌，避免混用上层TT目标、
固定禁用输出的board-link目标和下层lower-motor目标。下层F103目前仍需ST-Link；USB bootloader未实现。

2026-10-18后续：本页初次构建后已增加USB loader并安装偏移版PID APP，ESP32更新为`crtc-lower-pid-servo3-v3`。USB更新/读回、UART及版本核对通过，尚未执行本轮PID往复或舵机动作。当前实板状态以[后续说明](13-usb-loader-door-servo.md)和对应证据为准。
