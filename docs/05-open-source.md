# 开源项目、第三方代码与工具使用说明

截至 2026-10-18。按参赛手册要求说明使用范围，不将上游工程、厂商库、工具链或参考资料写成本队原创。以仓库已有文件声明和实际构建配置为依据，不用一份新许可证覆盖第三方原有声明。

## 上层车运动与遥控控制

来源：[Serialist/sample-mecanum-car](https://github.com/Serialist/sample-mecanum-car)，克隆基线提交 `1fa85bbdcd6c62f33f871284b642959cea975805`。本地目录 `sample-mecanum-car-study/` 保留源工程结构、Keil 工程与 [GPLv3 许可证](../sample-mecanum-car-study/LICENSE)。

原项目提供 STM32、PS2、麦轮混合和 PWM 的学习起点。团队后续修订/扩展零输出边界、输入检查、回中准备、DRV8833 后端、轮位方向、摇杆曲线、起停及校准保存。源码、构建脚本和测试公开，具体开发历程见 [上层文档](01-upper-car.md)。

## STM32 外设与启动支持

本地 `sample-mecanum-car-study/lib/cmsis/`、`lib/fwlib/` 随上述工程取得。文件头标明 ARM CMSIS Cortex-M3 与 STMicroelectronics STM32F10x Standard Peripheral Library；ST 外设驱动头标记 V3.5.0、2011-03-11，`core_cm3.h` 标记 V1.30、2009-10-30。保留各文件原版权和使用声明，不能把旧版支持文件整体称作本队代码或随意套用现代同名库许可证。

使用范围为芯片寄存器定义、时钟、GPIO、定时器、中断和启动支持。下层 `board-link` 复用相应基础设施及已有延时入口，不链接上层 PS2/DRV8833 主控制后端。

## ESP32 通信框架与构建依赖

ESP32 配置位于 [platformio.ini](../firmware/esp32/platformio.ini)：`espressif32@7.1.3`、Arduino-ESP32 2.0.17，依赖由构建环境安装，不把 `.pio/` 和工具链二进制复制入库。

- [Espressif Arduino-ESP32](https://github.com/espressif/arduino-esp32)：提供 ESP32 UART、USB、GPIO 等框架支持；构建输出解析的框架包版本为 `4.20017.260907+sha.dcc1105b`。
- [Espressif ESP-IDF](https://github.com/espressif/esp-idf)：上述框架底层依赖。UART 选脚与上拉实现的查阅版本为 v4.4.7，具体参考链接在接线文档中。
- [PlatformIO Espressif32 平台](https://github.com/platformio/platform-espressif32)：构建和依赖集成。本项目唯一显式 `lib_deps` 是仓库自身的 `firmware/shared` 符号链接库。

本仓库新增共享 C 分帧/协议、会话状态机、ESP32 诊断命令、STM32 UART/LED 入口和对应测试。COBS、CRC16 是采用的协议机制，不将这些通用算法本身宣称为原创发明。当前没有合并旧双 F103 USB 测试 PR，也未引入其 TinyUSB 固件作为生产依赖。

## 测试、烧录与资料工具

主机测试使用 Python 与 GCC；STM32 构建使用 Arm GNU，ESP32 使用 PlatformIO 和其工具链。硬件灯控脚本使用 [pyserial](https://github.com/pyserial/pyserial)。ST-Link/STM32CubeProgrammer 用于此前 STM32 写入和读回，Espressif 工具链的 esptool 用于 ESP32；使用工具不表示相关软件由本队开发。

芯片数据手册用于接口核对，不是复制到项目的控制实现。当前直接引用包括 ST STM32F103、Espressif ESP32-S3、Toshiba TB6612FNG 及 TI DRV8833 文档，链接保留在相关技术说明。SolidWorks 生成脚本及旧 CAD 作为历史设计资料；最新团队模型已原样归档于 `output/mechanics/team-solidworks-20261017/`，不能推定其来源与历史草案相同。

## 开源使用说明的维护

每篇参赛功能正文末尾说明其直接使用的开源项目，并链接本清单。若后续引入 BLE UI、编码器库、定位或路径项目，再补上作者/仓库、版本、文件范围、许可证和实际修改；不得提前将候选工具写成已集成。队员学习笔记摘编已经区分原项目知识、后续修订和未实现计划，见 [学习记录](03-learning-notes.md)。

## 团队桌面 BLE 面板贡献

桌面 `dashboard.html/css/js`、`dashboard-input.js` 及对应 Node 测试从协作者 Ehicy 的 [PR #6](https://github.com/Seroliya/zuiqiangchusheng/pull/6)（`97b631d76df932e2efe4dae2a7919c5fbb6dec17`）接入后继续适配。当前版本修正实测轮位和左右混控，新增里程计地图、场地图叠加及双舵机面板，共用现行 BLE 协议。PR 中的旧实板问题报告、旧接线工具与当时的浏览器测试保留在该 PR 历史中，不作为当前固件或接线依据。PR #6 由整合 PR #7 取代，不重复合入旧版面板。
