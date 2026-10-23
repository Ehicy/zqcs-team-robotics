# 双STM32应急接线历史材料

当前硬件为C3+STM32，请使用`../../../firmware/web/esp32c3-wiring.html`和`../../16-c3-dashboard-encoders.md`。

本目录保留应急双STM32时期的矩形图、模板、净表和组合键演示源码。`original-editor-template.html`为原始UI输入快照；Python生成器只写仓库内的历史fallback页面，不再依赖或改写某台电脑的外部目录。生成器重放的是历史方案，不能用它覆盖当前C3接线。

离线测试：仓库根运行`node --test docs/figures/fallback-20261019/test_editor.cjs docs/figures/fallback-20261019/test_controller_guide.cjs`。静态SVG属于当时的方案快照，不代表后续C3实物针脚。
