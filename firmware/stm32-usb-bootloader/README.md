# F103C8 USB bootloader

2026-10-18：ST-Link首次安装、Windows WinUSB自动绑定、USB下载及复位运行已通过；APP读回匹配、校准区不变、UART连通且输出禁止。尚未验收电机/舵机动作、任意掉电或长期稳定性。目标仅限 STM32F103C8T6 Blue Pill、8MHz晶振、PC13低有效LED、20KiB RAM/64KiB Flash。

F103C8的工厂ROM支持USART1下载，不支持USB DFU；本程序首次用ST-Link装到主Flash，以后使用板载USB更新。程序不提供USB串口，不会一直出现COM口；没有APP自动进入DFU命令，需要BOOT1跳帽和RESET。

| 地址/引脚 | 分配 |
| --- | --- |
| `0x08000000..0x08002000` | 8KiB loader（范围末地址不含） |
| `0x08002000..0x0800F800` | 54KiB APP；链接地址和VTOR同时偏移0x2000 |
| `0x0800F800..0x08010000` | 末尾2KiB校准预留区，USB底层拒绝擦写 |
| PA11 / PA12 | USB D− / D+，不可再挂其他GPIO负载 |
| BOOT0 | 保持0，启动Flash；置1是工厂ROM，不是此USB下载模式 |
| PB2 / BOOT1 | 置1后RESET常驻DFU；日常运行置0再RESET |
| PB15 | 启动汇编在C初始化前拉低TB6612 STBY；保留外部10kΩ下拉 |
| PA13 / PA14 | 保留SWD恢复 |

USB数据线、D+约1.5kΩ上拉到3V3、晶振与实物板型仍需核验。下载时BOOT1可留1；下载完成后拨回0并RESET运行。

## 编译与文件

仓库根目录执行，仅编译、不连接硬件：

```powershell
python firmware/stm32-usb-bootloader/build.py --toolchain D:\DEVELOP\CQU_AI\tools\arm-gnu-14.3\bin
python firmware/tests/run_usb_tests.py
```

输出 `output/usb-bootloader-20261018/`：

- `crtc-usb-loader.hex/bin`：只有loader；旧APP不能仍放在0x08000000运行。
- `lower-motor-usb.hex/bin/elf`：偏移版PID底盘APP。
- `first-install.hex/bin`：loader+偏移APP，首次ST-Link安装。BIN起址0x08000000，HEX自带地址。
- `lower-motor-usb.dfu.bin`：以后USB更新用的CRTCDFU1校验封包。不是ST DfuSe容器，不是裸BIN。
- `manifest.json`：产物尺寸及编译器（加`--hashes`才生成发布SHA256），不能代替板上版本确认。

原 `build_firmware.py --target lower-motor` 仍是直烧版。加 `--usb-bootloader` 后另输出 `build/lower-motor/usb/`，不覆盖直烧版。**普通直烧版BIN不可交给USB loader。**

## 首次安装流程（本板已完成）

1. **物理断开两块TB6612的VM正极**，托住舵机机构，主控稳定供电。USB 5V和降压5V不可无隔离直接并接；UART共地并避免未上电芯片被串口反供。连接/复位也可能改变当前输出，所以只读连接前同样需要VM断开。
2. ST-Link只读核对芯片ID、Flash容量和选项字（尤其WDG_SW），先备份完整64KiB与末尾校准页到仓库外。
3. 对 `first-install.hex` **按文件范围扇区擦除**写入并校验，不整片擦除、不改RDP/WRP/选项字。校准区不在文件中；若工具只支持整片擦除，停止并另定校准恢复方案。
4. BOOT0=0、BOOT1=1，接STM32 USB数据线并RESET；预期枚举为Maple DFU `1eaf:0003`。Windows可能需给**这个DFU接口**安装WinUSB/libusb驱动，不要替换ST-Link/ESP32驱动。脚本不自动装驱动。
5. BOOT1=0后RESET，核对ESP侧APP信息与锁定状态。继续保持VM断开，验证USB更新/读回、截断超长包拒绝、复位恢复、校准页一致性、PB15电平之后再接电机测试。

## 以后USB更新

默认只验证封包，不连接设备：

```powershell
python firmware/stm32-usb-bootloader/upload.py output/usb-bootloader-20261018/lower-motor-usb.dfu.bin
```

操作员确认VM断开后，提供dfu-util实际写入：

```powershell
python firmware/stm32-usb-bootloader/upload.py output/usb-bootloader-20261018/lower-motor-usb.dfu.bin --dfu-util D:\DEVELOP\CQU_AI\tools\dfu-util-0.11\bin\dfu-util-static.exe --write --motor-power-off
```

限定 `1eaf:0003`、interface0、alt2、1024B传输，不使用DfuSe `-s`。同一时间只接一块待刷F103 DFU板。工具成功仍须核对APP身份、锁定状态及读回；BOOT1留1时仍停在DFU，拨回0再RESET。

## 边界与限制

CRTCDFU1由一个1024B头和APP组成，头记录版本、起址、长度、头CRC32、APP CRC32。只收顺序1KiB块，末块可短；镜像4字节对齐、≤54KiB。禁止alt0/alt1/RAM写；底层再次检查Flash区间/对齐/错误位并读回。整包接收和Flash CRC验证后最后提交PC/MSP。SP、Thumb位、Reset_Handler范围检查。USB补实际包长、方向、SETUP/status长度和缓冲区边界，USB reset取消未完成会话。

这是CRC校验，不是签名安全启动，也不验证镜像来源。最后提交向量可防正常首页擦除完成后中断传输启动半包，但**不保证擦旧首页瞬间掉电的任意故障安全**；启动不做持久CRC。保留ST-Link，异常时connect-under-reset恢复loader+APP。必要时可BOOT0=1用工厂USART1恢复；PA9与M3编码器共用，先隔离编码器，当前ESP USART2不可直接代替。

loader不新启软件IWDG；硬件IWDG选项已启用时早期延长周期并持续喂狗，APP仍配置约200ms看门狗。只支持系统/硬件复位进入，禁止APP直接跳loader地址。

## 来源与验证

基础为 [STM32duino-bootloader](https://github.com/rogerclarkmelbourne/STM32duino-bootloader/tree/df689808b6030280480c0d151ee9c552ecf6b405)，固定commit `df689808b6030280480c0d151ee9c552ecf6b405`；保留LeafLabs MIT与ST原声明，来源及修改清单见 `UPSTREAM.json`。本项目重写main/DFU/update_core，并修改vendor硬件和USB边界。未使用上游包含Congratulations演示APP的组合BIN。

上游曾警告GCC4.9+可能改变旧寄存器代码行为。本候选GNU14.3显式volatile MMIO/PMA、关闭strict-aliasing，修正NVIC复位优先级UB；**构建/主机测试不能代替真实USB时序验收**。主机测试覆盖生产更新核、生产DFU状态机、516个Flash操作失败注入、顺序/CRC/截断/边界；不等于Cortex-M3执行、USB枚举或真实掉电验证。UBSan未运行。

本机补充：实板容量寄存器报告128KiB，仍按保守64KiB使用。bcdUSB=2.0、微软OS兼容ID描述符使Windows自动绑定内置WinUSB；alt0/1保留连续编号以兼容WinUSB，两者均禁写，只有alt2接收更新。成功整包写入后需BOOT1=0并RESET，WinUSB自动复位不可靠。dfu-util0.11缺标准DFU suffix及末尾can't detach提示，在本机返回0且数据/状态验收通过；不能只凭100%判成功，未来dfu-util兼容仍需处理。运行APP时D+拉低，DFU从设备列表消失。

`--motor-power-off`表示操作员确认电池已断开或两路VM已隔离，旧参数`--vm-disconnected`仍可用。现场备份保存仓库外work目录；实板记录见docs/evidence/usb-servo3-20261018.json。
