# 2026-10-23 启动静默与C3参数版（已烧录，USB诊断通过）

- C3 INFO：`crtc-c3-bootquiet-v5`；STM32 tag：`0xC3D91023`，普通0x08000000应用，不使用USB bootloader。
- 舵机信号：IO7大臂DS3218 270°、IO6小臂、IO10门。setup最开始GPIO输出低、下拉并切回GPIO IOMUX，开机1.5秒静默。LEDC仅在首次显式位置命令时挂接，绝不自动发送1500μs或重播保存参考。释放后停止LEDC、断开外设矩阵、输出低。
- 启动静默只管主控输出，不宣称关闭舵机内部固件功能。电源单独重启而C3继续输出旧PWM，舵机会追向旧目标；重新接入舵机电源前应先释放PWM。断蓝牙/STOP仍冻结最后指令并保持扭矩，避免机械臂自行下坠。
- USB 115200：`SERVO STATUS`只查询三路脉宽指令/目标/引脚电平；`SERVO OFF ALL`或`SERVO OFF 0/1/2`只释放，不提供USB定位命令。报告不是轴角测量或示波器测量。
- 参数唯一来源：C3 Preferences `crtc/params5`，128字节带版本/CRC的原子NVS记录。首次空记录才种子默认值，损坏记录不自动覆盖；写入失败不改RAM/STM。保存相同内容不重复擦写。烧录不执行erase_flash，不覆盖0x9000..0xdfff NVS。
- C3启动回读NVS并异步同步统一PID及4轮机械参数；Dashboard按同session/revision组装回读，忽略旧本机轮参数缓存，修改直接写C3 Flash。正在编辑的表单不被回读覆盖。kind7轮设置、kind8统一PID/NVS/同步状态、kind9舵机限位和参考。
- 两后轮首次默认电机符号改为+，轮序M1右前/M2左前/M3右后/M4左后，默认电机[+,-,+,+]、编码器[-,+,-,+]。之后以C3 NVS为准，刷新/升级不会重复反转。尚未进行实物方向验收。
- 编码器每次底盘启动自动逻辑归零；Dashboard“编码器清零”opcode12→UART type15，ACK252；保留PID/机械参数，重建测速历史。清零会结束当前录制、取消回放并重建里程基准，不删已保存动作；清零不修复输入噪声。
- 舵机范围/参考/门端点opcode13直接存C3，不触发运动。超机械范围的手动/动作库目标由C3再次检查。普通三线舵机没有实际角度反馈，首次参考错误仍可能大幅运动；不会假装支持实际当前位置相对控制。
- 保留v4：200ms S曲线启动；10ms撤驱动+最多30ms、至多5%反推/短路制动；可信反馈可用则PID辅助，不可信则前馈。通信失效撤驱动。软件制动不等于限流或保证40ms停稳。
- 动作/轨迹库保留原浏览器存储，不作为设备参数来源，没有删除用户动作。

供应商例程核对：`D:/normal program file/xwechat_files/wxid_zwyqegqfiizm22_75c4/msg/file/2026-10/03 舵机驱动例程.rar`已解到工作区`work/servo-vendor-20261023`，未执行例程。单舵机STM32 main.c数组显式初值1500，循环1000/2000；Arduino L_JXB.ino在setup写入初始1500；singleServo.ino attach后立即扫角。资料中未找到关闭舵机内部上电复位的协议。

验证：135项Node测试、4次真实ES模块DOM启动、C3参数/NVS失败与幂等保存/断电重建仿真/双板同步/编码器原点回绕测试、真实servo_output.cpp的GPIO/LEDC模拟测试通过。旧桥接/协议/电机/PID/速度/remote/编码器/fallback回归通过。Windows SANITIZE=0，未跑UBSan；仿真不等于实车。

安装记录：STM32写入/校验/复位成功，RAM 0x20000004读回0xC3D91023；C3四段写入校验成功。USB INFO为crtc-c3-bootquiet-v5，UART connected=1、flags=127、parser_errors=0、uart_faults=0。首次种子NVS loaded=1/revision=1，异步完成后synced=1，motor=1,-1,1,1，pid=1000,2000,0。C3硬复位后再次读取loaded=1/revision=1，约2.9秒时尚在同步，随后synced=1；三路pulses/targets/levels始终0，moving=0。没有发送位置或电机运动指令。详见flash-20261023/。用户随后仅接回大臂，并明确反馈“不会回中了”“完全胜利”：大臂上电不再自动回中已获得实机确认。小臂、门及完整带载动作仍未验收。编码器M2未供电环境下仍出现噪声计数，清零不解决该硬件问题。
