# STM32 + ESP32 双板通信与整车接线（2026-10-16）

下层车为 STM32 + ESP32-S3-N16R8、四个胶轮 MG370 编码器电机、两块 TB6612 双路板；2026-10-13 用户已确认两板通信线连接。新方案不用 PS2，Android＋BLE 静态网页面板已实现源码，本轮未烧录、实测或发布。上层 `sample-mecanum-car-study` 的 TT/DRV8833/PS2 工程继续使用；下层 `board-link` 与 `lower-motor` 独立构建，不能混用接线或烧录文件。

**已烧录的 `board-link` 基线已验证 UART 与 PC13 灯控，固定禁止电机输出。新 `lower-motor` 源码已实现 TB6612 开环、四轮编码器与 10ms PI 闭环，尚未烧录或实板动作。** 两个目标保留会话、序号、有效期和错误恢复；闭环增加编码器/方向/周期异常与驱动期间 500ms 无计数进展锁存，手机上限 300mm/s、PWM 上限 25%。里程计、导航和舵机动作尚未实现。新后端构建与主机测试见 [验证记录](04-validation.md)，操作入口见 [电机与编码器说明](06-lower-motor.md)、[速度闭环与手机遥控](08-speed-remote.md)。UART 和灯光验证不能代替电机、编码器或电源验收；UBSan 未运行。

## 2026-10-13 实机复检与烧录状态

以下全部为 **2026-10-13 电机禁用灯控基线的历史记录**，2026-10-16 上传前重新核对了测试与哈希；不能作为新增电机代码的烧录或实物证据。该次用户授权两端烧录及 ESP32 控制 STM32 板载灯，结果如下：

- 协议主机测试、旧代码回归和两端重新编译均通过。主机测试使用 `SANITIZE=0`；再次尝试 UBSan 仍在链接阶段报 `cannot find -lubsan`，不能计为通过。
- ESP32-S3 rev 0.2（MAC `C0:4E:30:2B:D9:A4`，Quad Flash 16MB、PSRAM 8MB）已写入 LED 诊断版，并独立读回应用区一致。当时含串口线路诊断的应用 BIN 为 289760 字节，SHA256 `7403d83eb69d8f5bc8611f8e44003bca0cd8963597f4f7d116f97b7f24c10149`。当时使用 CH343 的 COM8（USB 序列号 `5C93062224`）；另一原生 USB 接口曾为 COM4。端口号可变，每次下载须重新核对设备身份。
- STM32 UID `02430207505400113638414E` 的旧 USB 程序已确认来自 [PR #4](https://github.com/Seroliya/zuiqiangchusheng/pull/4)，版本 `0d2aea5f350d8ab311e5ea441ac17eb240691139`。原 `F103 USB Board Test` / `usb-board-test-v1` 是诊断程序，该 PR 明确尚未实现 USB bootloader/二进制烧录桥。SWD 备份的前 12888 字节与 PR 中旧固件 SHA256 一致；下载路径采用 ST-Link，不依赖旧 USB 串口。该 PR 不作为现行方案复用。
- ST-Link `37FF71064E57343655231443` 已核对上述 UID，并备份 128KiB 原 Flash。新版 STM32 BIN 为 5376 字节，SHA256 `b56641de24ed48b9aa50830fbf04984fd336244e3423139bd8361cbe6b3c43f8`，链接地址 `0x08000000`；已通过 ST-Link 写入并通过下载工具校验，第二次完整独立读回与 BIN 一致。第一次独立读回有字节差异，随后 SWD 又间歇出现无法读取 core ID；记录保留，连接稳定性仍需排查。
- 原始 Flash 完整备份在 `backups/board-link-20261013-01/`，当前 LED 版源码/产物、下载与读回证据在其 `led/` 子目录。2026-10-16 已清理被替代的诊断固件和早期副本，保留文件索引见 `led/manifest.json`。ESP32 原 Flash SHA256：`7c6d5c1b77f27982108adf2ce1394a09e5065c601b72e848d732d60ab5a013b7`；STM32 原 Flash SHA256：`87e4f13e325a8640531afa835e47e86572c274549a37714ed71b04a4a74e6363`。

**两端已烧录，ESP32→STM32 的三轮亮/灭命令与六次 ACK 实机验证通过。** 当前实际方向为 ESP32 **GPIO4 TX→STM32 PA3 RX，GPIO6 RX←PA2 TX**。检测时先把 ESP32 两个候选脚设为输入，GPIO6 连续收到 6 个 CRC 正确、序号递增的 STM32 STATUS，GPIO4 未收到帧；随后只调整 ESP32 软件方向，保留实物接线，未重烧 STM32。

验证期间连接令牌不变、解析/拒绝/串口错误均为 0，STM32 持续报告电机输出禁止、编码器有效位为 0；最后一条 LED OFF 已收到 ACK。ESP32 完整应用独立读回一致，读回工具复位 ESP32 后也重新握手成功。此结果确认真实串口往返与命令执行确认；2026-10-13 重复三轮测试再次收到全部六次 ACK，用户已目视确认板载灯亮灭，完成串口命令、确认回传与实际灯光的联合验收。目视观察与 ACK 分别记录，不把 ACK 当作光传感反馈。

本次 ST-Link 已接回并给 STM32 供电；借现有连接只读状态确认其正常运行和持续发帧，没有再要求插拔。灯控测试全部经 ESP32 USB 执行，不需要 ST-Link 参与通信，但 STM32 必须有独立可靠的供电，不能依赖 UART 信号线反向供电。最终源码/产物、读回、六次 ACK 和重连证据在 `led/esp32-uart4tx-6rx/`；引脚输入探测结果与源码在 `led/esp32-pin-map-probe/`。更早的 GPIO17/18、GPIO4 RX/GPIO6 TX 诊断产物和输入探测的临时固件已于 2026-10-16 清理，不再作为可用固件入口。

## 整车供电与逐线接法（2026-10-15）

当前采用 **2S2P、四节 18650 电池包 → 官方无线继电器受控输出 → 并联两路 DC-DC**：一路升压到 12V 供两块 TB6612 的电机电源，另一路降压到 5V 供 STM32、ESP32 两块开发板；舵机只预留位置。按普通满充 4.2V 的锂离子电芯计算，电池包标称 7.4V、满电 8.4V；2S2P 不会变成 14.8V。这项选择替代此前“两节 2S／2S2P 仅备选”的历史记录。

本节是端子功能接线表，不表示已确认各模块的实物端子排列。继电器沿用受控电源输出；未确认型号前不猜测 COM/NO、线圈或接收器电源的具体端子位置。普通非隔离升降压模块按下表共地。

```text
2S2P 电池包 → 总保险/开关 → 无线继电器受控输出＋
                                      │
                     ┌────────────────┴────────────────┐
                     │                                 │
                 升压模块 IN+                       降压模块 IN+
                 调至 12.0V                         调至 5.0V
                     │ OUT+                            │ OUT+
                  12V 分电点                         5V 分电点
                 ┌───┴───┐                         ┌───┴────┐
               TB① VM  TB② VM                  STM32 5V  ESP32 5V
                 │       │                         │
               M1、M2  M3、M4                  STM32 板载 3V3
                                                  ├─ TB① VCC
                                                  ├─ TB② VCC
                                                  └─ 四个编码器 VCC

继电器后另留一组舵机电源支路接口，暂不接负载；舵机稳压电压和电流容量待具体版本规格确认。
电池包输出负极、DC-DC IN−/OUT−、两开发板 GND、两 TB GND、编码器 GND 共地。
```

两路 DC-DC 的输入并联接继电器后的电池电压，不能把 5V 降压的输入误接成必须经过 12V 升压。继电器断开时两路都应断电；比赛运行时不另用 USB 绕过继电器给控制板供电。图中总保险/开关为电池正极保护位置，规格需按实际线材、电池包及继电器的直流能力选择。

### 电源逐端子表

| 起点 | 终点 | 用途 |
|---|---|---|
| 继电器受控输出＋ | 升压 IN+、降压 IN+，分别分线 | 两路电源输入 |
| 电池包输出负极/继电器模块电源负端 | 两模块 IN−、公共地分电点 | 电源回路；以模块丝印区分负端与触点 |
| 升压 OUT+，实测调好 12.0V | TB① VM、TB② VM，分别分线 | 四电机动力 |
| 升压 OUT− | 两 TB 的 GND/功率地 | 动力回流，线直接回分电点 |
| 降压 OUT+，实测调好 5.0V | STM32 开发板 5V、ESP32 开发板 5V，分别分线 | 两块开发板输入，不是 GPIO 或 3V3 |
| 降压 OUT− | STM32 GND、ESP32 GND | 控制板电源回流 |
| STM32 板载 3V3 | TB① VCC、TB② VCC | 驱动逻辑电源，采用 3.3V 逻辑 |
| STM32 板载 3V3 | 四个编码器 VCC，分别分线 | 用户资料为 3.3–5V、输出上拉至 VCC；采用 3.3V 信号，核对板载稳压总电流余量 |
| 两 TB 所有引出的 GND | 公共地，动力回路直接回分电点 | 驱动逻辑地/功率地都要连接 |

**一只 5V 稳压源给两板输入分线供电是本方案的正式接法；两板各自的 3V3 稳压输出不互接。** 12V 只进驱动 VM；5V 只进开发板标注且允许的 5V 输入，不能接芯片本体 VDD、3V3 或 UART/编码器信号脚。ESP32 实物为第三方板，5V 端子位置仍按实物丝印核对，不按照片遮挡部分猜针位。

外接 5V 运行时，先不同时接电脑 USB 的供电线或 ST-Link 的 3.3V 输出；本板 USB 防回灌电路尚未核实。需要 USB 调试时，可先断开外接 5V，改为两板各自 USB 供电并保持共地，或在确认电路后使用可靠的供电隔离。电机及将来的舵机动力正负线不经过面包板电源轨、细杜邦线或主控排针串接。

### 两块 TB6612 与四个电机

| TB6612 端子 | TB① 接法 | TB② 接法 |
|---|---|---|
| VM | 升压 12V OUT+ | 升压 12V OUT+ |
| VCC | STM32 3V3 | STM32 3V3 |
| GND | 公共地/动力回流 | 公共地/动力回流 |
| PWMA | STM32 PB6 | STM32 PB8 |
| AIN1 | STM32 PB0 | STM32 PB5 |
| AIN2 | STM32 PB1 | STM32 PB12 |
| AO1、AO2 | M1 左后电机的两根动力线 | M3 左前电机的两根动力线 |
| PWMB | STM32 PB7 | STM32 PB9 |
| BIN1 | STM32 PB3 | STM32 PB13 |
| BIN2 | STM32 PB4 | STM32 PB14 |
| BO1、BO2 | M2 右后电机的两根动力线 | M4 右前电机的两根动力线 |
| STBY | STM32 PB15 | STM32 PB15，与 TB① 共控 |

电机两根动力线成对接驱动输出，任何一根都不直接接公共地。这里的 M1–M4 是项目轮号，不猜六针插头颜色或方向；实际正转和编码器方向在后续架空逐轮验证。**启用 `lower-motor` 前，两 STBY 共接点必须外接 10kΩ 下拉到 GND，并确认模块没有将 STBY 硬接高电平；不能同时把它接 3V3 和 PB15。** 复位阶段引脚状态不能靠固件保证，下拉不能省略。`lower-motor` 使用 PB3/PB4 方向脚，关闭 JTAG 并保留 PA13/PA14 的 SWD。

### 四个编码器

每个编码电机的六根线分别属于动力两线和编码器四线：M+/M− 接 TB 输出；编码器 VCC、GND、A、B 分开接。

| 编码器 | A 相 → STM32 | B 相 → STM32 | GND |
|---|---|---|---|
| M1 左后 | PA0 | PA1 | 公共地 |
| M2 右后 | PA6 | PA7 | 公共地 |
| M3 左前 | PA8 | PA9 | 公共地 |
| M4 右前 | PB10 | PB11 | 公共地 |

**用户新商品图确认霍尔编码器为 13PPR、供电 3.3–5V、输出上拉至 VCC，故四个编码器 VCC 统一接 STM32 3V3。** 这使 A/B 输出与 STM32 3.3V 逻辑匹配；本接法不把编码器 VCC 接 5V 或 12V。六针插头的物理排列和线色仍未确认，必须先区分两根动力线及 VCC/GND/A/B，不能凭 MG370 名称或颜色猜测。接齐后测量 3V3 电压并核对板载稳压余量，驱动关闭时手转各轮核对计数和方向。

用户给出的减速比为 34:1、轮径图示 65mm。暂按 13PPR 是电机轴单相完整周期计算：前三轮 ×4 为 1768 计数/轮圈，第四轮 ×2 为 884。源码初值 `1768/1768/1768/884`、65mm 只是待验证假设；四轮 `confirmed` 均为 0，须实测整圈计数与正方向后显式确认。STM32 配置只在 RAM，重启恢复未确认；网页保存表单不会自动发配置或解锁。

### 板间通信与舵机预留

| 起点 | 终点 |
|---|---|
| ESP32 GPIO4，TX | STM32 PA3，RX |
| ESP32 GPIO6，RX | STM32 PA2，TX |
| ESP32 GND | STM32 GND/公共地 |

两板 UART 为 115200、8N1、3.3V 逻辑。PA1 已预留给 M1 编码器，不能用 PA1/PA2 替代串口 PA2/PA3。

| 舵机接口 | 信号 S | 电源接口处理 |
|---|---|---|
| 小臂（MG995） | ESP32 GPIO5 预留，当前未输出 PWM | 预留 V+/GND/S 三针；V+ 暂不接负载 |
| 大臂（DS3128） | ESP32 GPIO7 预留，当前未输出 PWM | 预留 V+/GND/S 三针；V+ 暂不接负载 |

从继电器后的电源分支预留舵机稳压器输入位置；待具体版本额定电压与电流确认，再决定共用当前 5V 分电点或使用独立舵机稳压支路。无论哪种，都从分电端给舵机供电，不经过 STM32/ESP32 的 3V3 或板载稳压器。GPIO4 已用于通信，不能继续作为旧翻臂舵机脚。

2026-10-17 用户确认：DS3128 用于大臂，MG995 用于小臂；具体转角版本、脉宽与机械限位尚未确认。两个信号线分别接 GPIO7（DS3128 大臂）和 GPIO5（MG995 小臂），本次没有分配夹爪舵机，电源负极与 ESP32 GND 共地；正极接符合对应舵机额定电压的稳压支路，不接 12V 电机支路或 2S 电池原电压。三线颜色不能替代厂商线序核对。接口分配不代表驱动已实现；本次没有上电试动，也没有刷入舵机固件。

### 当前能验证到哪里

用户本轮接受先做轻载台架测试。升压截图给出 `Iout_max = Vin × 6.3 / Vout`：满电 8.4V 升至 12V 约 4.41A，标称 7.4V 时约 3.89A，表内 7V 时为 3.67A。它是商家 MAX 口径，不能替代持续输出、温升、启动跌落及实际电池包放电测试；降压电流由用户表示充足，具体型号未记录。

项目此前的 MG370 商品资料为每只额定 1.1A、堵转 6.2A，四只额定点合计 4.4A；这不是日常轻载恒定耗电。TB6612 的每通道 1.2A 属绝对最大平均电流，3.2A 仅为 10ms 单脉冲条件，不能按“3.2A 连续”或承受 MG370 堵转来设计。当前接线关系用于后续逐轮低负载验证，尚未验收整车带载、爬坡或堵转能力。[东芝 TB6612 数据手册](https://toshiba.semicon-storage.com/info/docget.jsp?did=10660&prodName=TB6612FNG)

连接负载前先分别调好并测量 12V、5V 输出。先完成控制板与 UART 灯控，再接驱动逻辑及确认线序后的编码器；动力初试按 [电机与编码器说明](06-lower-motor.md) 逐轮架空进行。**当前已烧录的 `board-link` 仍强制禁止电机输出，接好全部线也不会驱动四轮。新 `lower-motor` 已接入开环、原始计数和 PI 闭环，但本次未烧录、未实板动作。**

## 本次通信接线

| ESP32 照片下方接口 | STM32 接口 | 方向 |
|---|---|---|
| GPIO4，UART1 TX | PA3，USART2 RX | ESP32 → STM32 |
| GPIO6，UART1 RX | PA2，USART2 TX | STM32 → ESP32 |
| GND | GND | 共地 |

115200 baud、8 数据位、无校验、1 停止位、无 RTS/CTS，3.3V TTL 逻辑。TX/RX 必须交叉，不能接 RS232，也不能给 ESP32 GPIO 输入 5V。

根据队员当时提供的实物照片：ESP32 模块/天线朝左、两个 USB-C 朝右时，底边排针可读到 `3V3、3V3、RST、4、5、6、7、15、16、17、18、8、3、46、9、10、11、12、13`，最右端 GND；其余遮挡脚不推断。上边 TX/RX 排不用于本次接线。当前 GPIO4/6 在该下排内；原 GPIO17/18 通信方案已由用户要求改接替代，实际收发方向另由 UART 收包验证确认。

台架测试时两板各自 USB 供电并共地；整车按上节从同一只 5V 降压输出分别接两板的 5V 输入，两板 3V3 稳压输出不互并。外部 5V 与 USB/ST-Link 供电的隔离要求见上节。电机 VM 和舵机动力支路不能经 UART 排针或核心板串接。

## 一致的资源分配

轮号沿用仓库：M1 左后、M2 右后、M3 左前、M4 右前。通信/接口常量集中在 `firmware/shared/board_link_config.h`。下表区分新 `lower-motor` 源码和已烧录的 `board-link` 基线；驱动代码存在不代表实物线序与动作已验收。

| 功能 | STM32F103C8 接口/资源 | 状态 |
|---|---|---|
| 板间 UART | PA2 TX / PA3 RX，USART2 | 两目标沿用；历史灯控基线已实测 |
| 板载 LED | PC13，低电平亮 | 独立诊断命令；启动/失联熄灭 |
| M1 编码器 A/B | PA0 / PA1，TIM2 CH1/CH2，4 倍计数 | `lower-motor` 原始采样；实板待验 |
| M2 编码器 A/B | PA6 / PA7，TIM3 CH1/CH2，4 倍计数 | `lower-motor` 原始采样；实板待验 |
| M3 编码器 A/B | PA8 / PA9，TIM1 CH1/CH2，4 倍计数 | `lower-motor` 原始采样；PA9 不作 UART |
| M4 编码器 A/B | PB10 / PB11，EXTI10 A 双边沿、读 B 判向，2 倍计数 | `lower-motor` 软件计数；最大脉冲率/漏计数待验 |
| 四轮 PWM | PB6 / PB7 / PB8 / PB9，TIM4 CH1/2/3/4 | `lower-motor` 20kHz；`board-link` 固定低 |
| TB① M1 AIN1/AIN2 | PB0 / PB1 | `lower-motor` 方向输出 |
| TB① M2 BIN1/BIN2 | PB3 / PB4 | `lower-motor` 方向输出，关 JTAG、保留 SWD |
| TB② M3 AIN1/AIN2 | PB5 / PB12 | `lower-motor` 方向输出 |
| TB② M4 BIN1/BIN2 | PB13 / PB14 | `lower-motor` 方向输出 |
| 两 TB STBY | PB15 共控 | `lower-motor` 受有效命令与失活保护控制；`board-link` 固定低，不与模块硬接高并用 |
| SWD 下载/调试 | PA13 / PA14 | 保留，不重映射为其他功能 |
| USB | PA11 / PA12 | 保留，本次未启用 |
| 模拟采样 | PA4 / PA5 | 空闲预留，本次未实现 |

TIM1/2/3 用于编码器，TIM4 独立用于电机 PWM，USART2 的 PA2/PA3 不占用 TIM2 编码器的 CH1/CH2。PB10/PB11 已给 M4，不能再分配 USART3 或 I2C2。PB6/PB7 已给电机 PWM，不能再选 USART1 重映射或 I2C1。旧 PS2 的 PB12–PB15 在新方案释放给方向/STBY；历史目标仍保留 PS2 代码。

ESP32 当前使用下排 GPIO4 TX、GPIO6 RX。GPIO4 已用于 UART；2026-10-17 将 DS3128 大臂舵机信号分配为 GPIO7，GPIO5 预留 MG995 小臂信号，均暂不配置 PWM。原 GPIO17/18 不再用于本次通信；其他可读普通脚仍空闲。GPIO3、GPIO46 是启动配置脚，不用于这版外设；GPIO0/45 同样避开。GPIO19/20 留给 USB，GPIO26–37 存储器相关脚不复用；N16R8 的 Octal PSRAM 尤其占用 GPIO35–37。通用 DevKit 名称不意味着实物板型已逐电路确认。

两块 TB6612 类型与编码器 3.3–5V 供电、输出上拉至 VCC 已有用户资料，采用上述 STM32 3V3 接法；模块 STBY 上拉情况、编码器六针线序、轮位极性及 13PPR 的实际整圈计数仍待核对。四轮编码器 A/B **都需要**。前三轮 4 倍、第四轮 2 倍，分别配置 `counts_per_rev`，不能共用一个倍频常数；34:1 与 65mm 是用户提供的初始参数，仍须实测校准。两 TB 的承载、电源容量和 MG370 电流余量没有因本次接口分配而获得验证。

## 协议与生命期

共享 C 实现：`shared/board_link.c`（帧）、`shared/board_link_node.c`（两端状态机）。不依赖 Arduino/ST 库，主机测试直接编译真实实现。

每帧是 `00 + COBS(body) + 00`，带前导/尾随零分隔符。解码后的 body：

| 字段 | 字节/约定 |
|---|---|
| version/type/payload_length/reserved | 各 1 字节，版本 1，reserved 必须 0 |
| sequence | uint32，小端，单方向共享递增序号 |
| session | uint32，小端，HELLO/ACK 为 ESP boot id，控制/状态/计数为 STM32 连接令牌 |
| payload | 最多 20 字节，按消息类型核验精确长度 |
| CRC | uint16，小端，CRC-16/CCITT-FALSE（poly 0x1021，init 0xFFFF，无反射/xorout），覆盖整个 header + payload |

| 类型 | payload | 行为 |
|---|---|---|
| 1 HELLO | 0 字节 | ESP 发起握手，STM32 接受新序号后回 ACK；本身不延长控制有效期 |
| 2 HELLO_ACK | 4 字节：STM32 令牌 | ESP 验 boot id 和服务器序号后建立连接；换令牌清除缓存输入 |
| 3 CONTROL | 12 字节：ttl_ms u16、request_enable u8、reserved u8、4×i16 请求轮速 mm/s | `lower-motor` 通过校准/反馈/故障门禁后交给 PI；`board-link` 仅保存、禁止输出。enable=0 时四目标须零；ttl 1–200ms，线协议范围 ±1000mm/s；手机与当前配置上限 300mm/s |
| 4 STATUS | 16 字节：flags/reason/2 reserved、last_control_seq/accepted/rejected 各 u32 | flags bit0 已握手、bit1 有效控制生命期、bit2 实际 STBY 禁用状态、bit3 支持开环 PWM、bit4 支持速度闭环；`board-link` 固定 bit2=1、bit3/4=0；能力位不是校准或实际运动证据 |
| 5 ENCODERS | 20 字节：valid_mask u8、reserved u8、sample_age_ms u16、4×i32 原始计数 | bit0–3 分别表示 M1–M4 样本有效；mask=0 表示无采样，任何零计数都不能据此解读为静止 |
| 6 LED_SET | 1 字节：0 灭 / 1 亮 | 已连接后控制 PC13，不刷新电机控制有效期 |
| 7 LED_ACK | 5 字节：请求 sequence u32、状态 u8 | 只接受当前会话中等待的请求序号和目标值 |
| 8 PWM | 12 字节：ttl_ms u16、request_enable u8、reserved u8、4×i16 有符号占空比‰ | 仅支持该能力的 `lower-motor` 执行；ttl 1–200ms，范围 ±250‰；禁用时四目标须零，禁用或全零会撤销会话；与 mm/s 消息独立 |
| 9 SPEED_CONFIG | 20 字节：wheel/flags、轮径 0.1mm、CPR u32、Kp/Ki 各 u16、max_mm_s/accel_mm_s2 各 u16、4 reserved | 停止时更新一轮 RAM 配置；flags 含电机反向、编码器反向和实测确认；收到 ACK 后才视为接受 |
| 10 CONFIG_ACK | 8 字节：请求 sequence u32、wheel/result/ready_mask/fault 各 u8 | 匹配当前配置请求；wheel=255 用于清故障；result=0 接受，非零拒绝 |
| 11 CLEAR_FAULT | 0 字节 | 仅禁用且有效反馈显示近零速度时清除锁存故障，不恢复旧运动请求 |
| 12 SPEED | 20 字节：ready_mask/fault/valid_mask/age_10ms、4×i16 实测速度、4×i16 斜坡目标速度 | mm/s 遥测；年龄超过限制失效，ready 与有效反馈分别报告 |

LED 一次只允许一个请求，500ms 内未完成即超时；控制/状态心跳优先于 LED 帧，换会话或失联清除待办并使 STM32 灯熄灭。ACK 表示已执行 GPIO 设置，不是光学反馈。

同一连接的序号必须严格更新；按 uint32 半范围规则允许自然回绕，重复/乱序拒绝且不更新控制或通信时间。校验、版本、长度、范围或保留位错误同样不刷新。未知类型拒绝。部分帧闲置 50ms 丢弃，超长噪声丢到下一分隔符，后续完整帧可恢复，粘包按分隔符逐帧处理。

控制每 50ms 一帧，状态/编码器接口各约 100ms，300ms 无有效同伴消息断联。控制有效期届满或串口错误后 STM32 撤销请求并换令牌，**在解析新到帧之前检查超时**，旧令牌的积压包不能恢复控制；重新握手后必须得到新令牌的新命令。HELLO、状态、计数或 LED 心跳不能延长 CONTROL/PWM 生命期。

ESP32 的兼容输入入口为 `src/control_input.h`；当前 BLE 通过 `ble_remote` 有界事件队列、`remote_control` 门禁和 `remote_bridge` 接入同一个 owner loop。没有有效输入时只发送禁用/零目标心跳。每次发送只使用原输入剩余有效期，重复 source_sequence 拒绝；输入过期、断联或换连接即清除。回调不并发修改双板状态机，STOP、生命周期变化和溢出优先于队列中的 DRIVE；缓存输入不会伪造新序号续期。

编码器采样适配入口为 `link_node_publish_encoders(counts, valid_mask, now)`。`board-link` 无采样，mask 始终为 0；`lower-motor` 发布硬件/EXTI 原始 32 位计数，同时按逐轮 CPR 和轮径换算轮速。有效位只说明驱动提供了有效样本，不证明接线、极性、换算或车体运动正确。样本年龄随帧传输，达到 200ms 自动失效；客户端也独立检查年龄，坏帧/旧序号不能延长样本生命期。原始计数仍保留 4x/2x 定义；位姿里程计尚未实现。

令牌用于连接代际隔离，STM32 使用 UID/周期计数及 ESP boot id 混合，不写 Flash；这不是加密认证，也不承诺跨断电的抗恶意重放。当前 ESP32 使用内置 BLE 库的单客户端 GATT，设备名 `CRTC-Lower-Car`，未加入 PIN 或配对身份认证；新连接必须重新读取状态并显式解锁。ESP32-S3 不支持经典蓝牙 SPP，本实现不使用 `BluetoothSerial`，也未开启 Wi-Fi。BLE 与网页协议见 [速度闭环与手机遥控](08-speed-remote.md)。

## 构建、测试与工程衔接

从仓库根目录执行（只编译/主机执行）：

```powershell
python firmware/tests/run_tests.py
python firmware/tests/run_speed_tests.py
python firmware/tests/run_remote_tests.py
python sample-mecanum-car-study/build_firmware.py --target board-link --toolchain '<Arm GNU bin目录>'
python sample-mecanum-car-study/build_firmware.py --target lower-motor --toolchain '<Arm GNU bin目录>'
pio run -d firmware/esp32
```

Windows 若 GCC 缺 UBSan 运行库，可设 `$env:SANITIZE='0'`；结果报告须注明。ESP32 使用已有 PlatformIO Espressif32 7.1.3 / Arduino 2.0.17 对应安装包，唯一项目库依赖是本仓库 `shared`。配置覆盖 N16R8 的 16MB Quad flash / Octal PSRAM。STM32 产物独立放在 `sample-mecanum-car-study/build/board-link/`、`build/lower-motor/`，ESP32 在 `firmware/esp32/.pio/build/board-link/`。旧 car/Keil 项目没有替换为新固件，默认 car 构建仍指历史版本；不得据此烧录下层车。

`board-link` 保持 UART/LED 与禁用输出行为；新 `lower-motor` 使用独立入口和 TB6612/编码器后端。两目标均不链接上层 TT/DRV8833/PS2 电机实现，不把旧弱反转急停移植到 TB6612。

`lower-motor` 输出限幅 ±250‰；开环斜坡 1000‰/s，PI 闭环每 10ms 更新、速度斜坡默认 500mm/s²，换向先过零并等待 50ms。零目标、STOP、失效和故障时撤去驱动，采用滑行；这不是机械瞬停。1kHz 时基发现主循环失活 50ms 后锁存关闭 STBY，IWDG 后备复位。闭环另锁存编码器、方向、周期异常及驱动期间 500ms 无计数进展；后者不是电流保护。上电不自跑，四轮闭环默认未确认；实际定时、电平、停止距离与负载表现仍需台架验证。

ESP32 根据 STATUS 能力位区分对端；旧禁用输出固件不支持 PWM 或闭环，不能靠更换命令使其驱动。后续先核对编码器线序，在驱动关闭状态手动转轮验证计数，再逐轮台架验证、确认参数并调试闭环与手机遥控。新固件和网页尚未烧录、实物验证或发布；里程计、导航和舵机执行仍待开发，构建与测试结果见 [验证记录](04-validation.md)。

## ESP32 控制 STM32 板载灯

ESP32 两个 USB 接口均支持 115200 baud 文本诊断：原生 USB 使用 HWCDC，CH343 使用 UART0；板间数据独占 UART1 的 GPIO4 TX/GPIO6 RX。命令以换行结束：`INFO`、`RX`、`HELP`、`LED ON`、`LED OFF`。灯命令先返回 `QUEUED`，收到 STM32 确认后返回 `ACK LED=0/1 seq=...`；失败返回 `ERR`。`INFO` 还包含串口收发字节/发送帧数、传输错误计数、最近错误事件、当前 RX 引脚电平、运行时间和实际 tx_gpio/rx_gpio，便于不依赖 SWD 排查板间链路。发送计数表示数据已提交 UART 驱动，不能代替对物理波形或对端收包的验证。`RX` 以十六进制返回最近最多 128 个接收字节，不清除计数、不发送板间控制命令。GPIO6 保持 UART 输入并显式开启内部上拉，避免未连接时悬空；不能由此保证外部连线正确。已显式启用 UART 帧错误中断，避免默认中断配置漏报错误停止位。

新电机开发版本另有 `PWM <8位十六进制token> <M1> <M2> <M3> <M4>`、`STOP` 和 `ENC`。四值单位为有符号千分比，不是速度；新输入最长有效 200ms，控制台不会给缓存值续期。STOP 以禁用 PWM 优先发送并撤销会话，旧文本 token 随即失效。每次有效握手都撤销旧运动会话，快速重连也不复用旧控制。使用方法和实测前提统一见 [开环电机说明](06-lower-motor.md)。下述历史灯控脚本检查“输出禁止”状态，不能用其成功结果证明电机功能正常。

两板供电、UART 连接并烧录完成后执行（Python 环境需有 pyserial，COM 号和 USB 序列号按实机填写）：

```powershell
python firmware/tests/test_led_hardware.py --port COM8 --serial 5C93062224 --report backups/board-link-20261013-01/led/esp32-uart4tx-6rx/led-test-repeat.json
```

日常验证只需 ESP32 USB，不需要 ST-Link。脚本验证三轮亮/灭命令与 ACK、序号、连接令牌、错误计数及电机输出禁止状态，成功结束时灯灭。加 `--swd-cli '<STM32_Programmer_CLI.exe路径>'` 可在 ST-Link 同时连接时独立检查每次 PC13 的输出配置、输出锁存与实际引脚电平。电平读回仍不等同于目视观察灯光；JSON 报告分别保留这些证据。

## 官方约束依据

- [STM32F103x8/xB 数据手册](https://www.st.com/resource/en/datasheet/stm32f103c8.pdf)：PA2/PA3 USART2、定时器、GPIO/SWD 复用。
- [Espressif GPIO 约束](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/gpio.html)：GPIO 矩阵、启动脚、USB 与 Octal 存储器占用。
- [ESP32-S3-WROOM-1 模块数据手册](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)：N16R8 Flash/PSRAM、BLE 与引脚说明。
- [Espressif 蓝牙架构](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/bt-architecture/overview.html)：S3 仅 BLE，当前采用 GATT 路线。

接收上拉实现依据：[Espressif IDF 4.4.7 uart_set_pin 源码](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/uart.c#L633-L665)；原生 IOMUX 路径与 GPIO 矩阵路径的上拉配置不同，本工程在选脚后显式设定接收上拉。


## 开源使用说明

STM32 目标使用 Serialist/sample-mecanum-car 学习工程的启动、延时和 CMSIS/ST 标准外设支持；ESP32 使用 Arduino-ESP32 2.0.17、PlatformIO Espressif32 7.1.3。协议与测试源码在本仓库提供，未合并旧双 F103 USB 测试 PR。来源、版本及修改范围见 [第三方清单](05-open-source.md)。完整原始 Flash 仅本地保留，公开验证记录见 [验证与证据索引](04-validation.md)。
