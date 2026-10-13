# F103 USB 与主副板测试记录

## 当前结果摘要

以下按时间保留部署记录，早期的“待测”描述属于当时状态；最新板间结果见文末。

- 主板 USB 收发已通过；两板固件均已用 ST-Link 下载并校验。
- 2026-10-09 简短联动检查：主板转发 PING、副板 UID 查询、LED TOGGLE 与状态查询通过，本次 UART / USB 错误未增加。
- 副板独立 USB 收发尚未单独完成；USB 经主板烧录副板和主板 USB 自升级均尚未实现。
- 2026-10-13 整理时同步了已合并的底盘与接线方案，台架改用独立的 64KiB 链接脚本；重新编译结果与已记录的两角色 BIN 校验值一致。本轮不执行硬件下载，结果不得当作新的实机验收。

部署及后续 USB 烧录路径见[方案说明](DUAL_F103_USB_PROGRAMMING.zh-CN.md)。

## 第一块 USB 测试（2026-10-08）

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

## 主板转发版再次烧录

用户随后要求直接烧好主板，再换接副板；减少重复验证。通过 SWD 读取 UID，确认仍是主板 `02430207505400113638414E` 后，下载提交 `f6f9e7d` 构建的 `usb-board-test-v1` 主板转发版，CubeProgrammer 校验成功并软件复位。BIN 长度 12888 字节，SHA256：

```text
85e93bcc3683a081a25f180f866a59ab4a2bed14bf62b4fa0b9dfd0e0c474dad
```

日志位于 `build/usb-test-board1-20261008/master-role-flash.log`。此次仅核对目标板并使用下载器校验，未做新的独立读回或重复大量 USB 测试。副板仍待用户换接后下载及简短实测。

## 副板烧录（2026-10-09）

通过 ST-Link 读取第二块 UID `02040200505400193638414E`，确认与主板不同。备份 131072 字节原 Flash，全为 FF；备份 SHA256 与上述空白 Flash 相同。CubeProgrammer 报告 3.26V，实际 SWD 240kHz。

副板 `usb-slave-test-v1`（BIN 13248 字节，上述 `a79acd…` SHA256）已下载、校验成功并软件复位，日志与备份位于 `build/usb-test-board2-20261009/`。

曾检测到副板 USB 为 COM4，序列号匹配上述 UID。启动简短 USB 测试时该端口已不在设备列表，未获得 PING 或灯控结果。主板 USB 和三根板间线需要接好后再做简短通信验证；不把 USB 枚举或烧录校验当作板间通信通过。

## 主副板简短通信 / 灯控实测

2026-10-09 00:37:27（UTC+8），实际通过 UID 确认 USB COM3 对应主板、ST-Link 对应副板。主板 USB 转发 REQ、副板 UART 应答 RSP；无需副板 USB 连接电脑。

| 检查 | 结果 |
|---|---|
| 编号与角色 | 主板 UID `02430207505400113638414E`，副板 UID `02040200505400193638414E`、`role=slave`，均匹配 |
| 简短请求 | `REQ 00000001 PING` 收到 `UART_RX RSP 00000001 PONG` |
| 灯控制 | `LED TOGGLE` 收到副板 `OK LED 1`，副板 INFO 的 `led` 从0变1 |
| 主板 UART 错误 | 测试前2、测试后2，本次新增0 |
| 副板 UART 错误 | 测试前0、测试后0，本次新增0 |
| 主板 USB 丢弃 | 测试前0、测试后0，本次新增0 |
| 简短测试结果 | `PASS`，未运行100次循环测试 |

首次 INFO 联测曾出现主板 `ERR UART LINE TOO LONG`；通过 SWD 查看副板，其正确收到了 REQ，已生成106字节合法 RSP、启动阶段3、串口错误0。主板当时累计 UART 错误2；后续 PING 和两次 INFO、灯控均成功，错误未新增。初次异常原因未单独验证，不将其记作固件已发现或已修复的缺陷。

测试脚本此前要求累计错误必须为0，已改为记录前后读数并比较本次变化；有新增错误或计数重置仍判失败。实测报告为 `build/usb-test-board2-20261009/quick-link-report.json`。独立副板 USB 收发未完成，实际灯外观未要求用户目测；这里验证的是板间指令、应答及副板报告状态。
