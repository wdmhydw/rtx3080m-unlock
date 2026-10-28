# Lenovo Legion 82K6: experimental dGPU-only Windows takeover

This experimental branch derives from `tobecsw/rtx3080m-unlock` commit `3bba414a85b20367509a9a14750e76f3d4abc922`. It records a single-machine result and hardware-specific research, not general Lenovo support or a validated installer. The original project README describes its independently displayed ASUS path; this branch adds a separate Lenovo dGPU-only experiment.

> **Verified on this machine: dGPU-only / direct NVIDIA display, no hybrid graphics required; automatic boot from the internal drive, no USB drive and no U key required.** After making the startup order persistent, a subsequent restart recorded a successful unlock and reached Windows with a working panel. This is source and research evidence, not a general-purpose installer.

## Tested configuration and result

- Lenovo Legion Y9000K 2021H / Legion 7 16ITHg6, model 82K6; firmware H1CN58WW; Intel Core i7-11800H.
- NVIDIA GeForce RTX 3080 Laptop GPU, Ampere / GA104M, 16 GB; PCI `10de:24dc`, subsystem `17aa:3f84`, PCI revision A1, chip identifier `b74000a1`, VBIOS `94.04.4f.00.1a`; firmware dGPU-only mode, with the NVIDIA GPU driving the laptop panel directly.
- Windows 11 Pro Insider build 26340, NVIDIA 621.01 (`32.0.16.2101`). Secure Boot was off. Display output in Windows was 2560×1600 at approximately 165 Hz.
- v12 is installed on the internal drive's EFI system partition and placed first in the firmware boot order; Windows Boot Manager remains available as a recovery entry. Normal startup needs no USB drive, boot-menu selection or U key.
- After configuring persistent startup, a subsequent restart on 2026-10-28 at 08:55 UTC+8 produced a new v12 EFI log recording a successful unlock, one FLR, SS0 `88888888`, SS1 preserved as `00000006`, released DMA mappings and a Windows handoff. The user confirmed Windows boot and normal display; the NVIDIA device reported error code 0.
- This is one successful restart after the persistent order was configured. Sleep/hibernate resume, cold boot and long-term stability remain untested. The unlock is reapplied at each full startup, not flashed permanently; no VBIOS flash or power-limit increase was performed.

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

## Internal automatic boot on the tested machine

The tested machine has the EFI application, locally prepared firmware assets and locally generated profile on the internal system drive's EFI system partition. The firmware boot order starts the unlock entry first, followed by Windows; the original Windows Boot Manager remains in the firmware menu as a recovery option. Windows Fast Startup was disabled for the test.

The panel is briefly black until the Windows NVIDIA driver takes over. A Windows startup task archives boot logs to prevent the 100 EFI log slots from filling; it is not responsible for unlocking. The machine-specific installation scripts include local disk and firmware identifiers and are not published. This branch does not currently provide a general installation, configuration or log-archive utility.

This branch changes firmware driver state and PCI/GPU state, resets the GPU and includes an automatic mode without the original project's physical U gate. It requires an unsigned EFI application and test-machine-specific preparation. Treat the code as a research artifact; hardware failures and recovery behavior are not universally characterized. Keep a separate Windows Boot Manager recovery path. Changes in firmware, GPU, display mode or driver topology require fresh validation.

## Source, testing and excluded data

The Windows-targeted build entry point is `bun tools/build-lenovo-unlock.ts --windows-takeover` with Bun and LLVM installed as described in the upstream README. The tested boot additionally used locally generated firmware assets and a locally generated runtime profile. There is no general-purpose provisioning workflow or binary release in this branch.

The host-side lifecycle tests require a **private** locally obtained PCI configuration fixture: `python tools/test_lenovo_cycle.py --pci-fixture <path-to-local-json>`, with `LLVM_BIN` pointing to LLVM's `bin` directory. The JSON must have a `PciConfig` array of 64 unsigned 32-bit words. Do not reuse another machine's captured configuration as proof of compatibility. The fixture used for the original tests is intentionally excluded, so the tests require appropriate local data.

Original license and third-party notices remain in the repository. NVIDIA firmware binaries, VBIOS/PROM captures, machine-specific profiles, raw logs, EFI/BCD/disk identifiers and locally installed runtimes are not included.
