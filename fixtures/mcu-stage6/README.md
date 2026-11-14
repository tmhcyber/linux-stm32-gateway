# 冻结MCU诊断主机参考

来源为MCU提交标识 `1164e5d7aec7bd6672199e024ebb71b24b98c475` 的实际C harness及编译依赖头文件。这里提供运行诊断格式器测试所需的最小源码集合，完整板端工程见[STM32终端仓库](https://github.com/tmhcyber/stm32f407-data-terminal)。

`host-result.json`记录编译源文件哈希，`vectors.bin`包含12条实际C输出；对应Linux测试fixture位于 `tests/diagnostic_vectors.py`。运行 `validate_stage6.py --mcu-reference fixtures/mcu-stage6`前按[测试指南](../../docs/TESTING.md)准备SOURCE快照。

MCU `APP_DIAGNOSTICS_ENABLED=0/1`分别测试遥测默认构建和显式诊断构建；这里的主机参考用于格式器、诊断字段和有效性验证。实板Flash哈希与读回记录位于[实板验收](../../docs/STAGE6_FINAL.md)。
