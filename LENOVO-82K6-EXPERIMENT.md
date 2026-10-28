# Lenovo Legion 82K6: experimental dGPU-only Windows takeover

This experimental branch derives from `tobecsw/rtx3080m-unlock` commit `3bba414a85b20367509a9a14750e76f3d4abc922`. It records a single-machine result and hardware-specific research, not general Lenovo support or a validated installer. The original project README describes its independently displayed ASUS path; this branch adds a separate Lenovo dGPU-only experiment.

## Tested configuration and result

- Lenovo Legion Y9000K 2021H / Legion 7 16ITHg6, model 82K6; firmware H1CN58WW; Intel Core i7-11800H.
- RTX 3080 Laptop 16 GB, PCI `10de:24dc`, subsystem `17aa:3f84`; the NVIDIA GPU drove the laptop panel directly during the tested boot.
- Windows 11 Pro Insider build 26340, NVIDIA 621.01 (`32.0.16.2101`). Secure Boot was off. Display output in Windows was 2560×1600 at approximately 165 Hz.
- One v12 automatic UEFI unlock and Windows handoff completed without a USB stick or a key press. EFI logs recorded one FLR, SS0 `88888888`, SS1 preserved as `00000006`, DMA mappings released, and the Windows handoff intent. The user observed Windows boot; the NVIDIA device reported no error.
- The persistent firmware boot order was configured and read back **after** that successful one-time boot; it has not yet been independently validated on a subsequent boot. Sleep/hibernate resume, power cycling and long-term stability remain untested.

Same-environment Windows GEMM comparison, 8192×8192, 5 warmups, median of 20 iterations; Python 3.12.12, PyTorch 2.10.0+cu128, CUDA 12.8 and the same NVIDIA driver:

| Type | Before (TFLOPS) | After (TFLOPS) | Change |
|---|---:|---:|---:|
| FP16 | 38.51 | 55.24 | +43.44% |
| BF16 | 37.87 | 53.05 | +40.08% |
| TF32 | 19.31 | 18.62 | -3.57% |
| FP32 | 11.46 | 11.26 | -1.75% |

Six small exact-integer matrix checks passed with zero reported absolute error. These are single-run results; neither application speedups nor broad numerical correctness is established by this benchmark.

## What changed and what was learned

The original ASUS configuration keeps an independent physical display active during the unlock. This Lenovo experiment takes down the NVIDIA UEFI graphics path, removes a pathless virtual console GOP during isolation, performs the unlock and one FLR, verifies resource and cleanup state, and then hands control to Windows without restarting the NVIDIA firmware GOP. The Windows NVIDIA driver subsequently initializes the panel. This is specific to the observed firmware topology and its fingerprint checks.

An earlier v11 attempt completed the unlock transaction but restarting the NVIDIA UEFI GOP returned `EFI_NOT_STARTED`, leaving a black screen before Windows handoff. v12 avoided that firmware restart; a single successful Windows boot and the benchmark above followed. Black screen alone does not establish that an unlock or handoff succeeded.

This branch changes firmware driver state and PCI/GPU state, resets the GPU and includes an automatic mode without the original project's physical U gate. It requires an unsigned EFI application and test-machine-specific preparation. Treat the code as a research artifact; hardware failures and recovery behavior are not universally characterized. Keep a separate Windows Boot Manager recovery path. Changes in firmware, GPU, display mode or driver topology require fresh validation.

## Source, testing and excluded data

The Windows-targeted build entry point is `bun tools/build-lenovo-unlock.ts --windows-takeover` with Bun and LLVM installed as described in the upstream README. The tested boot additionally used locally generated firmware assets and a locally generated runtime profile. There is no general-purpose provisioning workflow or binary release in this branch.

The host-side lifecycle tests require a **private** locally obtained PCI configuration fixture: `python tools/test_lenovo_cycle.py --pci-fixture <path-to-local-json>`, with `LLVM_BIN` pointing to LLVM's `bin` directory. The JSON must have a `PciConfig` array of 64 unsigned 32-bit words. Do not reuse another machine's captured configuration as proof of compatibility. The fixture used for the original tests is intentionally excluded, so the tests require appropriate local data.

Original license and third-party notices remain in the repository. NVIDIA firmware binaries, VBIOS/PROM captures, machine-specific profiles, raw logs, EFI/BCD/disk identifiers and locally installed runtimes are not included.
