> 2026-10-18 舵机v4跟进：移除舵机确认复选框，首次接管显式起点参考，ESP32限速/加速度生成PWM。
> 编译及主机测试完成，尚未烧入；旧v3安装记录不等于v4已安装。见 docs/13-usb-loader-door-servo.md。

## 2026-10-23 当前可用版本

下层车为 **常规四胶轮差速 + STM32F103 + LuatOS ESP32-C3**。入口：[当前接线与操作](docs/16-c3-dashboard-encoders.md)、[Dashboard](firmware/web/dashboard.html)、[C3完整接线台](firmware/web/esp32c3-wiring.html)。

- 已烧录：C3 `crtc-c3-bootquiet-v5`，底盘 `0xC3D91023`；PID/轮向/编码器机械参数由C3 Flash加载并下发，网页修改直接保存C3。
- 大臂IO7、小臂IO6、门IO10；上电无PWM、不恢复参考角度，三路启动/重启零输出已读回，用户确认仅接大臂后不再自动回中。
- 曲线快起快停、编码器启动清零/按钮清零、可信反馈PID辅助与异常读数前馈后备、舵机命名位置/积木和相对路径示教已实现。其他舵机、带载完整行程和路径精度仍需实车验证。
- [已安装镜像、诊断与验收记录](output/bootquiet-encreset-20261023/README.md)；先前S3、双STM32和v3/v4镜像作为历史保留，不能混用端口。

以下为此前方案与验收历史；当前端口和固件以本节及C3操作文档为准。

# CRTC2026 双车机器人项目

参赛队名：最强初生；公开仓库为 [Seroliya/zuiqiangchusheng](https://github.com/Seroliya/zuiqiangchusheng)，默认分支 `main`。参赛文档统一阅读 [中期考核技术文档](docs/14-midterm-report-20261019.md)；新增当前C3操作/验收文档；其余旧说明与证据通过 Git 历史追溯。

更新：2026-10-18。现状依据用户核对、当前源码、实板测试及 [2026CRTC参赛手册V1.5](2026CRTC参赛手册V1.5.pdf)。本文件保存当前方案、进度与继续工作的入口；历史过程见功能文档和备份。

## 1. 目标与两车分工

团队参加新生组，采用上层车推落、下层车接收与收集的分工。

| 项目 | 上层车 | 下层车 |
|---|---|---|
| 任务 | 推落挂架及中央黄块，交给下层车收集；无夹爪 | 接收上层推落的方块，并通过翻转臂、夹爪收集 |
| 电机、轮组 | 4 个 TT，麦克纳姆轮 | 4 个 MG370 编码器电机，胶轮 |
| 电控 | STM32F103、2 块 DRV8833、PS2 | STM32F103C8、ESP32-S3-N16R8、2 块 TB6612 |
| 操控 | 原 PS2 遥控方案 | 电脑/Android BLE 网页面板已实现；电脑实板直行、转向短测通过，Android 实连待验收 |
| 当前进度 | 原方案已能行驶、横移通过 150mm 桥面；尚未进行新一轮改进 | 建模、底盘、机械机构及料仓已完成；5V、12V 支路已接通，逐轮及四轮 20%/25%、各 5s 架空试转通过；间歇掉线/疑似复位仍待定位 |
| 后续方向 | 以速度为重点，研究提高稳定性 | 中期后按“四目标导航＋人工末端调整”实现路径自动 |

上层原车的行驶、横移过桥结果不能套用到下层 MG370 胶轮车。上层当前供电细节本轮未重新核对，下文 2S2P 与 12V/5V 分支专指下层车。

## 2. 下层车当前状态

### 机械与安装

- 用户实测下层车尺寸（2026-10-17）：前后轮中心距 130mm、左右轮中心距 165mm、整车长×宽 240×190mm、轮径 65mm；车已落地，用户表示空间足够。165mm 仅为转向等效轮距初值，仍需地面校准。网页已用用户提供的 V1.5 场地图原图作为底图，内框映射 2400×2400mm、单格 300mm；当前车位映射为所选红方 a1/蓝方 h8 格中心，默认红方且车头朝场内，这是人为坐标约定，不是相机定位。图像资源和像素配准见 `firmware/web/competition-field.js`、`assets/crtc-field-v15.png`；46 项网页/地图逻辑测试通过，自动导航实车轮次尚未执行。

- 最新机械文件已于 2026-10-17 收到：16 个 SLDPRT 和 `装配体1.SLDASM` 原样归档至 `output/mechanics/team-solidworks-20261017/`，附 SHA-256 清单；未重建装配、核查全部引用或验收运动干涉。更早 `flip-arm-sw-v*` 草案不替代本次模型。
- 用户不负责机械；机构尺寸、运动范围与干涉由机械负责人或最新模型核对。
- 当前采用面包板搭建；中期考核后改用洞洞板。
- 舵机：DS3128 为用户确认的 360°位置型大臂舵机，接 GPIO12；MG995 为 180°位置型小臂舵机，接 GPIO13。此前ESP32 `crtc-lower-servo-v1` 已烧录，上传哈希和 INFO 版本核对通过，未独立读回 Flash。50Hz PWM 开机关闭；显式 1500μs 启用，调试窗口 1300–1700μs、单次最多 25μs、间隔至少 100ms。底盘锁定且四轮新鲜反馈为零才接受调节；STOP/断联保持最后脉宽，显式释放才关闭 PWM。没有实际角度反馈，机械限位和带载能力待验收。
- DS3128 的 1500→1525→1500μs 短测及释放通过，用户确认转动并返回；MG995 同幅度往返及释放有回执/状态确认，目视结果待确认。重试曾在动作前因状态变化取消，未发舵机命令；随后稳定停机 2 秒再测完成。证据见 [DS3128](https://github.com/Seroliya/zuiqiangchusheng/blob/7af2521e83984f6dec2a5cd384b27fa92b80138e/docs/evidence/servo-ds3128-test-20261017.json)、[MG995 复测](https://github.com/Seroliya/zuiqiangchusheng/blob/7af2521e83984f6dec2a5cd384b27fa92b80138e/docs/evidence/servo-mg995-retest2-20261017.json)。

### 电源

```text
4 节 18650 组成 2S2P 电池包
  → 保险 / 手动总开关 → 官方无线继电器受控输出
      → 统一升压至 12V → 并联分支
          ├ 两块 TB6612 的 VM → 四个 MG370
          └ 降压至 5V → STM32、ESP32 开发板的 5V 输入
STM32 板载 3V3 → 两块 TB6612 的逻辑 VCC、四个编码器 VCC
各模块 GND 共地；两块主控的 3V3 稳压输出不互并。
```

- 普通 4.2V 满充电芯的 2S2P 为标称 7.4V、满电 8.4V；12V 支路需要升压。
- 2026-10-17 用户确认四轮架空、两块 TB6612 和四个电机已接好，VM 动力支路已接通并共地；已逐轮短时试转，尚未验收带载电源能力。
- 用户接受先做轻载台架测试。按商家升压截图公式，8.4V→12V 的 MAX 约 4.41A，7.4V 时约 3.89A；持续输出、温升及整车负载能力尚未验证。降压电流由用户表示充足，型号未记录。
- 电机动力电流直接走可靠分电端与回流线，不经过主控板或面包板电源轨；舵机供电按具体版本的额定电压与电流安排，尚未带载验收。
- STM32 必须有正常电源。此前移除 ST-Link、未另接电源时曾出现双灯变暗，不能把 UART 反向供电当作正常供电。
- 2026-10-17 首测前用户确认 STM32 已由降压模块 5V 供电，ST-Link 下载线已拔掉；正常运行不依赖 ST-Link。
- 外部 5V 与电脑 USB/ST-Link 电源同时连接的防回灌条件尚未核实，按 [完整接线说明](firmware/README.md) 操作。


### 当前 PID、USB 与三舵机（2026-10-18，双板已更新）

- 用户明确：四轮在 STM32 中各自计算 PID，同一约10ms节拍，共用一组初始 Kp/Ki/Kd，但每轮的积分、测速与微分滤波状态互相独立；PID中间量不回传ESP32。
- ESP32保持常规桥接/遥测程序 `crtc-lower-pid-servo3-v3`，启动或UART会话恢复后异步下发统一参数，收到STM32成功ACK前不能解锁。初值1000/2000/0（milli），仅RAM；ESP32重启恢复初值，不自动确认机械校准。
- 整车状态：等待链路→等待PID→等待校准→锁定→已解锁→运行；异常进入故障或等待链路，恢复不自动续跑。D对测速求导并采用30ms一阶低通；停车/故障/改参数清除I/D历史。
- Dashboard新增统一PID调节、可调单程距离/次数的前后往复测试及折叠调参说明。往复流程仅在网页运行，ESP32不运行测试专用程序；前台在线、已确认配置及新鲜反馈是启动条件。
- 用户于2026-10-18明确反馈驱动板、电池和升压器已通过带载验证；本次按该反馈开放目标轮速至1000mm/s、闭环PWM上限100%，允许明确确认后的超额定目标。保留25%开环诊断上限及全部超时/故障保护。该验证来自用户报告，本轮未重新执行实物测试。
- 额定260rpm为参考，65mm轮约885mm/s。往复测试默认使用四轮配置中最小最高目标的100%，接近端点减速；编码器打滑/滑行影响距离，不能把端点偏差当PID轮速超调。
- 详情、协议与构建/测试入口见 [统一PID与Dashboard往复测试](https://github.com/Seroliya/zuiqiangchusheng/blob/7af2521e83984f6dec2a5cd384b27fa92b80138e/docs/12-unified-pid-test.md)。双板已更新：STM32安装8KiB USB loader与偏移PID APP，USB下载/读回及校准页保留通过；ESP32 v3写入与INFO通过，UART连通且输出禁止。未做本轮运动/舵机角度实测。以下旧实板运动记录仍是之前PI/50%基线。

### 固件与已验证功能

- 当前 STM32 为独立 `lower-motor` 电机目标；历史 `board-link` 目标仍固定禁用电机，二者不能混用。ESP32现为`crtc-lower-pid-servo3-v3`，包含常规BLE/回传、统一PID下发与三舵机。此节后续旧运动记录不等于新版运动验收。
- 板间 UART：**ESP32 GPIO4 TX→STM32 PA3 RX，GPIO6 RX←PA2 TX，GND 共地；115200、8N1、3.3V 逻辑。** PC13 灯控、超时/重连和应答已有实板验证。
- 实测轮位：M1 右前、M2 左前、M3 右后、M4 左后，编码器与同编号电机配对；左侧 M2/M4，右侧 M1/M3。前进定义为轮胎顶部朝车头移动，电机方向 `[+1,-1,+1,-1]`、编码器方向 `[-1,+1,-1,+1]`。粗测 CPR `[1756,1791,1774,898]`、轮径 65mm，仍需精确标定。来源见 [手转与电动校准](https://github.com/Seroliya/zuiqiangchusheng/blob/7af2521e83984f6dec2a5cd384b27fa92b80138e/docs/evidence/manual-encoder-calibration-20261017.json)。
- 编码器前 3 路 TIM2/3/1 四倍计数，第 4 路 EXTI 两倍计数；每 10ms 按实际间隔换算轮速并执行 PI。四轮出厂配置 `confirmed=0`，只存 RAM；网页缓存只保存表单，不自动发送或解锁。初始 PI 为 kp=1000、ki=2000（milli），加速度 500mm/s²，参数需实车调试。
- TIM4 四路 20kHz PWM；开环上限 25%，闭环硬上限 50%，网页速度可配置至 300mm/s。50% 是允许上限，不代表已验收满占空比带载。反转过零后等待至少 50ms；停止时滑行，不等于机械瞬停。TB6612 共用 STBY 需外接 10kΩ 下拉且不能硬接高电平。
- 有效期最长 200ms，缓存输入不续期；坏帧/旧序号不刷新期限，重连不续跑。编码器、方向、控制周期异常和驱动期间 500ms 无计数进展会锁存故障；主循环失活 50ms 禁止输出，IWDG 后备复位。普通 STOP 不清故障，故障恢复需要明确操作。
- 看门狗启动问题已修复并有下载/读回、握手、灯控、故障注入和复位证据，见 [修复记录](https://github.com/Seroliya/zuiqiangchusheng/blob/7af2521e83984f6dec2a5cd384b27fa92b80138e/docs/11-lower-watchdog-fix.md)。M4 后续故障现场确认 IRQ 预算超限，加入 20μs 脉冲过滤、32 IRQ/ms 预算及现场记录，并移除 WFI 保持 DWT 连续计时；修正版烧录/读回一致。100mm/s 左右转复测用户确认方向正确、转停正常且无故障；长期稳定性和输入干扰仍待验证，不能据短测认定根因唯一，见 [M4 证据](https://github.com/Seroliya/zuiqiangchusheng/blob/7af2521e83984f6dec2a5cd384b27fa92b80138e/docs/evidence/m4-clock-fix-20261017.json)。
- 已有逐轮、双轮、三轮及四轮架空短测；四轮 20%/25%、各 5 秒用户确认转停。电脑 BLE 80mm/s、约 2 秒直行在独立 USB 电源下获用户确认四轮向前并停车；网页提速和左右转短测也已确认。历史间歇失联/复位与原外部 5V 供电问题未完成长期验收，不能将灯亮或一次成功当作电源稳定证明。证据集中在 `docs/evidence/`。
- 桌面 `firmware/web/dashboard.html` 基于 PR #6 面板继续适配：WASD/方向键、空格/Esc 停止；手机 `index.html` 共用 BLE 协议、校准与舵机组件。电脑实连已测试，Android 实连和公网部署仍待验收。
- `odometry.js` / `live-map.js` 已实现网页编码器里程计、场地图叠加与轨迹显示；只是位置估计，尚未用于自动控制。停车锁定且反馈新鲜才能确认起点，断联/会话变化/参数修改/异常采样冻结轨迹。BLE 计数没有源时间与启动编号，不能保证识别所有复位；四胶轮擦滑会产生误差，实际精度待测。
- 半自动仍处于方案与地图准备阶段：STM32 定距离/定角度动作、导航里程计、任务协议和路网执行尚未实现；网页黄色 600mm 往返线只是预览，没有自动行驶验收。尺寸已知，见机械段及 [四目标方案](https://github.com/Seroliya/zuiqiangchusheng/blob/7af2521e83984f6dec2a5cd384b27fa92b80138e/docs/07-semi-auto-plan.md)。
- 2026-10-18 合并前复验：协议、桥接、开环、速度环、遥控、编码器和上层回归测试，49 项网页测试，以及 STM32 lower-motor / ESP32 构建。UBSan 因本机缺运行库未运行，不计通过；本轮不重新烧录或驱动实物。

## 3. 近期工作与验收

- 下层优先验证供电和通信长期稳定性，精确标定 CPR、轮径与等效轮距，测闭环跟踪及失联停止；既有短时成功不等于持续带载、温升或堵转能力通过。
- 核对 MG995 小范围往返目视结果，再逐路标定舵机方向、机械限位与脉宽角度映射，随后验收带载；最新 CAD 已归档，机构尺寸/干涉由机械负责人确认。
- 半自动先做可取消的单动作与 600mm 低速直行校准，再做左右 90° 和两点往返，最后接四个目标。红蓝方地图、小路网与“自动到站→人工微调→回停靠点实际对齐→确认位置”仍按既定方案推进；手动接管清空任务，重连不续跑。
- 上层保留能行驶、横移过桥的 TT 基线，后续研究速度与稳定性；下层中期后从面包板改用洞洞板。
- 所有记录区分源码、构建、写入校验与实物验收；测试细节保留在功能文档和证据中，不以历史哈希证明后续固件身份。

## 4. 规则与中期考核

当前本地规则依据为 V1.5；后续修订及官方答疑优先。

| 项目 | 当前要求 |
|---|---|
| 日程 | 中期材料 2026-10-19 24:00 截止；瑞士轮 10月24–25日，淘汰赛 10月31日 |
| 中期构成 | 完成度、规则测评、文档评估归一赋分后按 40%/30%/30% 加权；规则测评 80 道单选，每队一次机会 |
| 功能展示 | 基础指标见手册第9页，按队伍所有机器人合计；其中多舵机项要求同一台车至少三个舵机且非初始姿态保持。未完成全部基础指标不能凭进阶功能加分；允许中期不产出实物演示 |
| 参赛文档 | Markdown 放根目录 `docs/`，以具体功能 H2 标题组织，记录按完成时间从早到晚的开发历程，文末完整说明使用的开源项目；提交 GitHub/Gitee 仓库地址 |
| 提交材料 | 邮件列明完成指标及仓库地址；视频 MP4、至少 1080p/30fps，打包 ZIP；收件人、标题及包名格式见手册第11页 |
| 预检录 | 正式比赛前一周开放预约，每队不超过两次；按正式检录流程检查机器人，便于提前发现问题并整改，具体安排见组委会通知 |
| 初始外廓、重量 | 不超过 290×195×210mm、2.0kg；展开尺寸按手册允许的包络方案完整核对 |
| 电源与断电 | 18650，每车一个对外输出接口的供电模块，额定电压不超过 7.4V；正式比赛适配官方无线继电器 |
| 方块 | 外形 30mm、内部镂空 15mm；官方打印工艺约 11.29g/块。V1.5 已修正中期方块尺寸，无需保留旧 40mm 冲突 |
| 成本 | 新生组造价不推荐超过 400 元，报销最多 400 元；无线继电器按规则例外处理，非造价硬上限 |

通用遥控条款允许蓝牙等控制方式，但中期底盘运动指标仍写 PS2；手机 BLE 替代 PS2 的等效考核认定待官方确认。机械细节跳过核对，不据旧舵机方案判断当前多舵机指标是否满足。

## 5. 代码、构建与资料入口

| 路径 | 用途 |
|---|---|
| [中期考核技术文档](docs/14-midterm-report-20261019.md) | 队伍提供的中期考核正文（2026-10-19） |
| [整车接线说明](https://github.com/Seroliya/zuiqiangchusheng/blob/7af2521e83984f6dec2a5cd384b27fa92b80138e/docs/lower-car-wiring.md) | 下层供电、TB6612/编码器/UART 接线及历史灯控证据 |
| [电机与编码器说明](https://github.com/Seroliya/zuiqiangchusheng/blob/7af2521e83984f6dec2a5cd384b27fa92b80138e/docs/06-lower-motor.md)、[速度闭环与手机遥控](https://github.com/Seroliya/zuiqiangchusheng/blob/7af2521e83984f6dec2a5cd384b27fa92b80138e/docs/08-speed-remote.md)、[半自动方案](https://github.com/Seroliya/zuiqiangchusheng/blob/7af2521e83984f6dec2a5cd384b27fa92b80138e/docs/07-semi-auto-plan.md) | 台架校准、闭环/BLE 入口与后续四目标导航计划 |
| [firmware/shared/board_link_config.h](firmware/shared/board_link_config.h) | 下层引脚、协议参数及开环 PWM 限幅的统一入口 |
| `firmware/shared/`、`firmware/esp32/`、`firmware/web/` | 双板协议/PI/遥控状态机、ESP32 PlatformIO 工程、Android BLE 静态面板 |
| `sample-mecanum-car-study/examples/board-link/`、`sample-mecanum-car-study/examples/lower-motor/` | 下层 STM32 禁用输出基线与独立开环/闭环电机目标入口，按构建目标区分输出能力 |
| `sample-mecanum-car-study/hardware/Src/` | 上层原 TT/DRV8833/PS2 驱动与主循环 |
| [版本索引](sample-mecanum-car-study/docs/LOCAL_VERSIONS.zh-CN.md)、[S曲线与停车说明](sample-mecanum-car-study/docs/S_CURVE_STOP.zh-CN.md) | 原车固件追溯、构建和操作说明 |
| `docs/` | 仅保留 `14-midterm-report-20261019.md`；旧功能文档、图片与证据见 Git 历史 |
| `output/mechanics/` | 最新团队模型见 `team-solidworks-20261017/`（附 SHA-256 清单）；旧 v1–v3 不能自动作为当前机构依据 |
| `backups/` | 历史快照、固件和实机证据，仅供追溯 |

在仓库根目录构建，先安装对应工具链；不要混用两车目标和引脚：

```text
python sample-mecanum-car-study/build_firmware.py --target car --toolchain "<Arm GNU bin目录>"
python sample-mecanum-car-study/build_firmware.py --target board-link --toolchain "<Arm GNU bin目录>"
python sample-mecanum-car-study/build_firmware.py --target lower-motor --toolchain "<Arm GNU bin目录>"
pio run -d firmware/esp32 -e board-link
```

上层、下层协议主机测试入口分别为 `sample-mecanum-car-study/tests/run_tests.py`、`firmware/tests/run_tests.py`；PI 与遥控核心另见 `firmware/tests/run_speed_tests.py`、`firmware/tests/run_remote_tests.py`，集成验证见验证记录。若因缺 UBSan 设 `SANITIZE=0`，报告应注明；本轮 UBSan 未运行。下载前核对目标板、对应产物和供电状态，按各工程说明保留备份并校验。

## 6. 维护原则

- 默认在 `main` 集中维护，不为每次小改动另建分支；确需临时分支时，完成后合并并删除。废弃或已吸收的 PR 关闭，保留历史链接；不删除协作者 fork 的分支。

- [PROJECT.before.md](PROJECT.before.md) 保留 2026-10-16 清理前的项目快照，已公开供历史追溯；其中过时方案和进度不覆盖本文件。

- 新事实确认后直接更新当前章节；测试细节、下载日志和开发历程写入对应功能文档或证据目录。
- 上层原 TT 方案继续使用；下层旧 TT/DRV8833/PS2、双 F103 和 PCB 底板规划已由当前双板胶轮方案、面包板转洞洞板安排替代。
- 待确认的机械参数、模块规格与实测结果明确标注，不从旧讨论推定已经实现。
- `backups/` 保留当前双板原始 Flash、最终固件及实机证据，以及最近一次主文件清理快照；上层回退固件保留在 `sample-mecanum-car-study/releases/firmware/`。过时文档/CAD 备份及被替代的诊断固件已清理，历史备份不作为现行项目状态入口。

2026-10-18更新：第三路MG995用于开门（约90°），分配ESP32 GPIO14/LEDC2；原两路GPIO12/13。三路网页角度滑块已加入并平滑跟随，门1000–2000μs参考端点可调整；大/小臂原首测窗口不变，角度待标定。USB当前WinUSB自动绑定、alt2整包更新与复位启动已验证，首次完整128KiB备份在仓库外work/usb-bootloader-20261018/hardware。用户偏好后续只保留必要写入读回，不反复哈希。详见[USB与三舵机](https://github.com/Seroliya/zuiqiangchusheng/blob/7af2521e83984f6dec2a5cd384b27fa92b80138e/docs/13-usb-loader-door-servo.md)。
