# 历史 BLE 输入草稿（不参与构建）

2026-10-16：用户确定 Android 手机与 BLE 网页面板，随后改为先完成电机驱动。
这里保留尚未集成的输入解析草稿，不参与 ESP32 或主机测试构建。
`run_remote_tests.py.draft` 缺少配套测试 C 文件，也未随目录移动修正路径；不是可用测试入口。
该阶段没有完成 BLE 服务与手机网页。后续实现已位于 `firmware/shared/remote_control.*`、`remote_bridge.*`、`firmware/esp32/src/ble_remote.*` 和 `firmware/web/`，运行和验证请看 [现行说明](https://github.com/Seroliya/zuiqiangchusheng/blob/7af2521e83984f6dec2a5cd384b27fa92b80138e/docs/08-speed-remote.md)。本目录仅保留历史，不作为当前实现或可用测试入口。
