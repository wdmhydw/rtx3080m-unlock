# 联想 82K6 独显直连 Windows 接管实验

这是基于 `tobecsw/rtx3080m-unlock` 提交 `3bba414a85b20367509a9a14750e76f3d4abc922` 的独立实验分支。[English](LENOVO-82K6-EXPERIMENT.md)

## 本机配置与结果

- 联想拯救者 Y9000K 2021H / Legion 7 16ITHg6，型号 82K6，BIOS H1CN58WW，i7-11800H。
- RTX 3080 Laptop 16GB，PCI `10de:24dc`，子系统 `17aa:3f84`；测试时内屏由独显直接输出。
- Windows 11 专业版 Insider 26340，NVIDIA 驱动 621.01，关闭 Secure Boot；Windows 显示 2560×1600，约 165Hz。
- 一次 v12 实验启动自动完成解锁并进入 Windows，无需 U 盘或按键。日志记录 SS0 `88888888`、SS1 保持 `00000006`、一次 FLR、DMA 映射已释放，以及 Windows 交接意图；用户确认进入 Windows，显卡设备状态正常。
- 持续自动启动顺序是在这次成功启动后设置并读回核对的，**尚未验证设置后的下一次启动**。睡眠、休眠、断电重启及长期稳定性尚未测试。

在相同 Python 3.12.12、PyTorch 2.10.0+cu128、CUDA 12.8 和 NVIDIA 驱动环境下，8192 阶矩阵、预热 5 次、取 20 次中位数：

| 类型 | 解锁前 | 解锁后 | 变化 |
|---|---:|---:|---:|
| FP16 | 38.51 TFLOPS | 55.24 TFLOPS | +43.44% |
| BF16 | 37.87 TFLOPS | 53.05 TFLOPS | +40.08% |
| TF32 | 19.31 TFLOPS | 18.62 TFLOPS | -3.57% |
| FP32 | 11.46 TFLOPS | 11.26 TFLOPS | -1.75% |

六项小规模整数矩阵检查的最大绝对误差均为 0。以上是单次合成测试，具体应用收益和长期数值稳定性需要进一步测试。

## 关键经验

原 ASUS 项目依赖独立核显维持 UEFI 画面。本机独显直连实验需要释放 NVIDIA 固件显示路径及一个无设备路径的虚拟控制台 GOP，然后解锁、执行一次显卡功能级复位并核对资源与清理状态。

v11 虽然完成了解锁事务，但重启 NVIDIA UEFI 显示驱动时返回 `EFI_NOT_STARTED`，黑屏且未进入 Windows。v12 在复位后不重启固件显示驱动，而是将启动交给 Windows，由 NVIDIA Windows 驱动接管屏幕，随后完成一次成功启动。黑屏本身不能证明解锁或系统交接成功。

## 源码与验证范围

构建入口为 `bun tools/build-lenovo-unlock.ts --windows-takeover`，依赖 Bun 和 LLVM。本机实测还使用了本地生成的固件素材和运行时 profile；此分支没有通用安装程序。模拟生命周期测试需要用户自行提供私有 PCI 配置文件：`python tools/test_lenovo_cycle.py --pci-fixture <本地 JSON 路径>`，并设置 `LLVM_BIN`。原测试使用的机器配置文件未公开。

该实验修改固件驱动和显卡状态、执行复位，自动模式取消原项目的物理 U 确认；仅有上述单机实测，不能视为其他机型的兼容性保证。原项目许可和第三方声明保留，NVIDIA 固件、PROM、机器专属 profile、原始日志和本机启动标识均未发布。
