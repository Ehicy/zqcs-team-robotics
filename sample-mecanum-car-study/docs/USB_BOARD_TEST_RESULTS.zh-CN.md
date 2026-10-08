# 第一块 F103 USB 测试板记录

日期：2026-10-08。用户确认：两块新板仅作测试，当前第一块只接 ST-Link，没有其他外设。

| 检查项 | 实际结果 |
|---|---|
| ST-Link / SWD | 已连接，实际 240kHz；CubeProgrammer 2.23.0 |
| 供电 | 下载器报告 3.26V，未作为万用表实测值 |
| 器件 | Device ID `0x410`，Rev A；下载器报告 128KiB，编译仍限制 C8 的 64KiB Flash / 20KiB RAM |
| 芯片 UID | `02430207505400113638414E` |
| 原 Flash | 备份 131072 字节，全为 `FF`；原板没有已烧录应用 |
| 编译 | Arm GNU GCC 14.3.1；应用代码启用 `-Wall -Wextra -Werror`，通过 |
| 占用 | Flash 12884 字节，静态 RAM 3004 字节；链接器另留至少 2048 字节栈 |
| 下载 | HEX 经 ST-Link 烧录，CubeProgrammer 校验成功并复位 |
| 独立读回 | 12884 字节，与 BIN 的 SHA256 一致 |
| 启动运行 | SWD 热连接读到 `usb_test_boot_stage=3`，`usb_test_ticks=14364`；USB 初始化完成并进入主循环 |
| 时钟 | `RCC_CR=0x03038283`、`RCC_CFGR=0x001D040A`，HSE / PLL 就绪，PLL 为系统时钟 |
| USB 识别 | 核心板连接 USB 后，Windows 识别 `USB 串行设备 (COM3)`，VID:PID `CAFE:4001`，状态 OK；序列号与 UID 一致 |
| USB 收发 | 100 次 PING / PONG、100 次带序号文本 ECHO 全部通过 |
| 灯控制指令 | ON / OFF / TOGGLE / AUTO 回复均符合预期；未要求用户目测灯 |
| 异常输入恢复 | 非法命令、200 字符长输入、含 NUL 输入均被拒绝，后续 PING 正常 |
| 错误计数 | `uart_rx=0`、`uart_errors=0`、`usb_drops=0`；串口未接线 |
| 第二块与板间串口 | **待测**：尚未下载第二块及交叉连接 PA2/PA3 |

固件版本：`usb-board-test-v1`。固件 BIN SHA256：

```text
338957573be53c87a1ca7077105c7ec90424de5325566a3b70b076a5d4e34d13
```

原 Flash 备份 SHA256：

```text
b5a41c3758763bbec72769fab4a2533bf2db0b6312d93d25a695f9e4b9e02260
```

本机原始日志与备份位于 `build/usb-test-board1-20261008/`，USB 实测报告为其中的 `usb-report.json`（2026-10-08 23:53:04，UTC+8），构建产物及依赖哈希位于 `build/usb-board-test/manifest.json`，均为本地忽略文件。源代码、命令及接线步骤见[测试教程](../examples/usb-board-test/README.zh-CN.md)。

编译、下载、Flash 一致性、启动及 USB 主机收发均已通过。实际灯状态由用户目测确认；第二块及双板通信需要完成对应接线后验收。

## 主副板测试程序准备

用户后续指定第一块作为主板、第二块作为副板。第一块保留已烧录的 `c41e2e5` 版本；本轮读取 COM3 的 INFO 正常，UID 匹配，错误计数仍为 0。

副板角色通过 `build_usb_test.py --role slave` 构建，固件 `usb-slave-test-v1`，Flash 13248 字节、静态 RAM 3148 字节，符合 64KiB / 20KiB 限制。BIN SHA256：

```text
a79acd0e0fd941db4efddd16874d5cbb88c6ae7d9a1be2b99a3b296ffe3bf568
```

主机测试直接编译并调用固件的 `slave_protocol.c`：有效请求、最大/大小写编号、0 编号、错误格式、逐字节截断、非请求响应等测试通过。主板角色也已重新编译验证；本轮未操作下载器、未烧任何板。

现有主板原 Flash 实测读回仍保存在 `build/usb-test-board1-20261008/readback.bin`，上文首次烧录 SHA256 对应此文件。源码扩展后重新构建的主板 BIN 可不同，不能当作本轮已烧录。第二块与 `slave_test.py` 的双板实测结果仍待完成。
