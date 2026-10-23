# 2026-10-23 可测试构建（两板已烧录）

19:27（UTC+8）安装核对：ST-Link 已写入并校验 STM32；COM9 已写入并校验 C3 四段镜像。STM32 Flash 标记读回 `0xC3D71023`；C3 INFO 为 `crtc-c3-servo270-v3`，`transport=1 connected=1 parser_errors=0 rejected=0 uart_faults=0`，ENC 四路计数可回传。未发送车辆或舵机运动指令，实车方向、编码器标定和动作精度仍待测试。
烧录日志：`flash-20261023/`。本次用户确认电机 VM、三路舵机正极及 C3 外部 5V 已断开。

- C3：`esp32/firmware.bin`，`crtc-c3-servo270-v3`；三舵机信号IO7/IO6/IO10，270°大臂全范围能力位。
- STM32：`stm32/dashboard-motor.bin` / `.hex`，标记0xC3D71023；PA引脚不变，持续编码器采样、逐轮前馈+置信PID融合。
- 两端默认电机方向M1/M2/M3/M4为+ / − / − / −（本次只把M3右后反相）；编码器方向不随电机反相自动更改。
- 页面：`http://127.0.0.1:8080/dashboard.html?v=teach-v2-m3`；图：`http://127.0.0.1:8080/esp32c3-wiring.html?v=8`。
- 新增命名位置、三路机械臂状态、动作图、WASD/舵机示教录制与相对轮程复刻。前台浏览器运行，不是MCU离线轨迹播放器。
- 131项Node测试、4次离线真实入口DOM启动（含两次M3迁移）、继电器图DOM测试、C3/PA/自适应C主机测试通过；历史C回归通过。
- C3和STM32构建成功，另编译历史lower-motor确认宏隔离；Windows C测试SANITIZE=0，UBSan未运行。
- 四轮负载仿真只证明模拟条件下的收敛，不是实车标定；轨迹算法不能恢复同侧双编码器缺失时的真实位姿，也不能识别全部滑移。
- 上述构建测试阶段未操作硬件；随后已完成本页开头记录的两板烧录。C3换口、全270°范围与STM32融合控制现已安装，未执行运动验收。
- 现机M3可先通过Dashboard停车写入方向参数验证；旧C3的RAM参数掉电会恢复旧默认。刷新后旧本机参数会一次性反转M3并保留其他值。

详细操作、算法和论文：仓库 `docs/16-c3-dashboard-encoders.md`。
