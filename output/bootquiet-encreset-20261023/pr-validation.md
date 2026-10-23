# 2026-10-23 PR #9 全量更新复验

PR分支：`codex/20261022-c3-dashboard-direct-drive`，基于上游main `c608684`。沿用既有PR #9，不创建重复PR；保留上游中期文档与清理结果，不把被上游删除的全部旧docs树恢复。

从PR checkout复测通过：135项Node网页测试、4次真实ES模块离线DOM启动、C3 V8接线端点/方向/继电器测试、5项历史矩形接线/组合键HTML测试；10套Python/GCC测试（C3/NVS/舵机HAL、桥接、协议、电机、PID、速度、remote、编码器、fallback、USB）、ST-Link位置桥纯主机测试、原PS2工程回归；STM32 dashboard-motor及C3 c3-dashboard均重新编译通过。Windows SANITIZE=0，未运行UBSan。本轮发布复测未连接硬件、未再次烧录或执行运动。

当前已安装的实板版本与证明：C3 `crtc-c3-bootquiet-v5`、STM32 `0xC3D91023`，两板UART在线；NVS硬复位后加载并最终同步，三路PWM/目标/引脚读回为0。用户明确反馈“不会回中了”“完全胜利”，仅接回的大臂上电无自动回中获得实机确认。小臂/门、全负载行程、四轮实际方向/闭环效果和路径复刻精度未在此验证。

主要新增：设备参数以C3 Flash为唯一来源、四轮独立PID和可信编码器自适应后备、200ms S曲线启动/限时制动、两后轮符号持久修正、编码器启动和按钮清零、三舵机限位/参考/命名姿态和积木顺序、相对路径示教/复刻、继电器与完整物理端口接线台、USB只读/释放诊断。

可刷镜像、早前版本快照和源代码全部保留；不提交node_modules、work scratch、宿主测试.exe或未应用的浏览器表单备份。供应商RAR例程只读分析，未执行或复制发布。新增历史图生成器输入模板，默认仅写仓库内页面。

构建/测试原始输出在本目录`pr-validation/`；Flash/USB原始证据在`flash-20261023/`，用户验收在`hardware-validation.json`。纯文字日志保留工具原始空格，`.gitattributes`不把其排版空格当源码空白错误。
