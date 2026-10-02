# 本地固件版本索引（2026-10-02）

当前开发分支：`local/chassis-tuning-20261002`。当前板内版本：`chassis/scurve-stop-20261002`，已烧录并独立读回校验，可以试车。Git 检查点和标签在本机创建；最新分支用于向主仓库提交PR，主分支保留此前合并状态。

最新 [PR #2](https://github.com/Seroliya/zqcs-team-robotics/pull/2) 已创建，包含当前代码、历史版本索引、四份 HEX 与验证说明，等待审核。当前固件对应源码提交 `c14c55a12e232da9e4421bd50c9f4b458ddf6aa5`；后续文档提交不改变该固件。

## 四个可以回溯的底盘版本

| Git 标签 | 主要行为 | BIN | 验证及实车状态 |
|---|---|---|---|
| `chassis/openloop-20261002` | 摇杆幅度调 PWM，0.5秒线性起步，80ms换向等待，无横移学习 | 7260字节 | 已有烧录及独立读回；前后正常，横移走弧线 |
| `chassis/learn85-100-v1-20261002` | 前85/后100试验补偿，SELECT人工辅助学习，Flash版本1 | 9820字节 | 烧录、参数保存/复位加载通过；用户反馈偏移加重，保留作历史对照 |
| `chassis/pad100-95-v2-20261002` | 前100/后95，左25%/右50%三次混合映射，0.5秒线性起步，Flash版本2 | 10036字节 | 已烧录并独立读回；是本次改急停前的回退版本 |
| `chassis/scurve-stop-20261002` | 保留上一版映射和校准，0.5秒五次S曲线，松杆/START弱反转＋短路制动 | 11220字节 | 主机测试、编译、烧录及独立读回通过；实际停车效果待试车 |

前三版从原有备份恢复成独立 Git 提交，并使用 Arm GNU 14.3 **逐版重新编译；每版 BIN 都与当时保存的固件逐字节一致**。没有把最新源码套到旧版本名称下。源码与固件对应关系、SHA256、原始备份路径见 [版本登记表](../releases/local-versions.json)。

这些提交是在2026-10-02恢复历史快照时新建的，提交日期不代表当时试车的准确时间。历史说明保留当时语境，当前操作状态以本页及[S曲线与急停](S_CURVE_STOP.zh-CN.md)为准。

## 旧固件与诊断程序在哪里

| 用途 | 现成文件或入口 |
|---|---|
| 当前S曲线急停 | [scurve-stop-20261002.hex](../releases/firmware/scurve-stop-20261002.hex) |
| 回退到上一版摇杆映射 | [pad100-95-v2-20261002.hex](../releases/firmware/pad100-95-v2-20261002.hex) |
| 旧横移学习试验 | [learn85-100-v1-20261002.hex](../releases/firmware/learn85-100-v1-20261002.hex) |
| 早期开环 | [openloop-20261002.hex](../releases/firmware/openloop-20261002.hex) |
| 按○翻转PC13灯，检查手柄通信 | `examples/ps2-led/main.c`，构建参数 `--target ps2-led` |
| 四键分别启停四电机，确认轮位 | `examples/motor-buttons/main.c`，构建参数 `--target motor-buttons` |

此前已合并的基础代码标记为 `chassis/base-20260930`，其中包含两种诊断入口。圆圈全轮、肩键调速、初始档位调整等过渡试验的原始 HEX、读回文件和日志仍在各自 `backups/` 目录；登记表的 `legacy_archives` 列出了电控历史目录及文件校验值。原始备份保留，未将64KiB整片读回当作某个单独应用BIN。

## 怎么查看或回到旧版

在仓库根目录查看历史：

```powershell
git log --oneline --decorate local/chassis-tuning-20261002
git tag --list 'chassis/*'
git diff chassis/pad100-95-v2-20261002 chassis/scurve-stop-20261002 -- sample-mecanum-car-study/hardware
```

如果要继续修改旧版，先保存当前改动，再从它新建试验分支，例如：

```powershell
git switch -c trial/pad-linear chassis/pad100-95-v2-20261002
```

切换代码不会改变芯片内的程序；仍需编译、下载。只想比较旧固件，可直接选择上表对应HEX，下载前断开两块VM正极，保留控制电源；下载完成后关电源、接回VM再上电。回到当前开发分支：

```powershell
git switch local/chassis-tuning-20261002
```

三版历史重新编译日志和版本恢复记录在 `backups/local-versions-20261002-01/`；当前下载证据在 `backups/curve-stop-20261002-01/`。标签用于明确保存版本，日常修改继续在开发分支提交。
