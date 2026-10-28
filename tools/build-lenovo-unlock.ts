import assert from "node:assert/strict"
import { createHash } from "node:crypto"
import { resolve } from "node:path"
import Bun, { $ } from "bun"
import { inspectPe } from "./verify_pe"

const root = resolve(import.meta.dir, "..")
const windowsTakeover =
  process.argv.length === 3 && process.argv[2] === "--windows-takeover"
const automatic =
  windowsTakeover || (process.argv.length === 3 && process.argv[2] === "--auto")
if (process.argv.length > 2 && !automatic)
  throw new Error(
    "Usage: bun tools/build-lenovo-unlock.ts [--auto | --windows-takeover]",
  )
const output = windowsTakeover
  ? "build/lenovo-windows-takeover"
  : automatic
    ? "build/lenovo-dgpu-auto-unlock"
    : "build/lenovo-dgpu-unlock"
const options = process.env.LLVM_BIN
  ? { PATH: process.env.LLVM_BIN }
  : undefined
const clang = Bun.which("clang", options),
  link = Bun.which("lld-link", options)
if (!clang || !link) throw new Error("Set LLVM_BIN to clang and lld-link.")
const sources = [
  "src/gop/app",
  "src/gop/lenovo_cycle",
  "src/gop/reset",
  "src/gop/windows",
  "src/gop/visual",
  "src/unlock/runtime",
  "src/unlock/format",
  "src/unlock/app",
  "src/unlock/app_io",
  "src/unlock/ownership",
  "src/unlock/sha256",
  "src/unlock/checks",
  "src/unlock/port",
  "src/unlock/compat",
  "src/unlock/vendor/unlock_610",
  "src/unlock/vendor/prep_610",
  "src/unlock/vendor/layout_610",
  "src/unlock/vendor/vbios_610",
  "src/unlock/vendor/gfw_eval",
  "src/unlock/runtime_profile",
]
const flags = [
  ...(automatic ? ["-DGC_LENOVO_AUTOSTART"] : []),
  ...(windowsTakeover ? ["-DGC_LENOVO_WINDOWS_TAKEOVER"] : []),
  "-target",
  "x86_64-pc-windows-msvc",
  "-std=c11",
  "-ffreestanding",
  "-fno-builtin",
  "-fno-stack-protector",
  "-mno-red-zone",
  "-fshort-wchar",
  "-ffunction-sections",
  "-Os",
  "-Wall",
  "-Wextra",
  "-Werror",
  "-Wno-unused-parameter",
  "-Wno-unused-function",
  "-DGC_LENOVO_CYCLE",
  "-DGC_LENOVO_VIRTUAL_RELEASE",
  "-DGC_LENOVO_UNLOCK",
  "-DGC_FULL_UNLOCK",
  "-DGU_GOP_EXPERIMENT",
  "-D__KERNEL__",
  "-DGU_UEFI_PORT",
  "-DGU_RUNTIME_PROFILE_BUILD",
  "-DGU_SSD_EXPERIMENT",
  "-DGC_WINDOWS_HANDOFF",
  // Keep the proven SS0-only algorithm. GxIndependentRun and its display path
  // are not linked; Lenovo ownership is checked by LcRun/PatchIsolated.
  "-DGU_INDEPENDENT_DISPLAY",
  "-I",
  "src/unlock/compat",
  "-I",
  "deps/edk2/MdePkg/Include",
  "-I",
  "deps/edk2/MdePkg/Include/X64",
]
await $`mkdir -p ${output}`.cwd(root)
const objects: string[] = []
for (const source of sources) {
  const object = `${output}/${source.replaceAll("/", "_")}.obj`
  await $`${clang} ${flags} -c ${source + ".c"} -o ${object}`.cwd(root)
  objects.push(object)
}
await $`${link} /nologo /nodefaultlib /subsystem:efi_application /entry:efi_main /machine:x64 /base:0 /dynamicbase /nxcompat /timestamp:0 /opt:ref /opt:icf ${"/map:" + output + "/BOOTX64.map"} ${"/out:" + output + "/BOOTX64.EFI"} ${objects}`.cwd(
  root,
)
const pe = await inspectPe(
  resolve(root, output, "BOOTX64.EFI"),
  resolve(root, output, "BOOTX64.map"),
)
const map = await Bun.file(resolve(root, output, "BOOTX64.map")).text()
for (const symbol of [
  "LcPrepare",
  "LcRun",
  "GxPrepare",
  "GxPatch",
  "GxObserve",
  "GxSafe",
  "GxFinish",
  "GuFlr",
  "gpu_unlock_610_unlock",
  "GwPrepare",
  "GwStart",
])
  assert(new RegExp(`\\b${symbol}\\b`).test(map), `Missing ${symbol}`)
for (const symbol of [
  "GfRun",
  "GfResetPci",
  "GuSingleOwnerRelease",
  "GxIndependentRun",
  "GuProfileCreate",
  "GuProfileRefresh",
  "GuProfileCommit",
])
  assert(
    !new RegExp(`\\b${symbol}\\b`).test(map),
    `Unexpected execution code ${symbol}`,
  )
const records = await Promise.all(
  [
    ...sources.map((x) => x + ".c"),
    "src/gop/lenovo_cycle.h",
    "src/gop/cycle.h",
    "src/gop/patch.h",
    "src/unlock/boot_source.h",
    "tools/build-lenovo-unlock.ts",
  ].map(async (path) => ({
    Path: path,
    SHA256: createHash("sha256")
      .update(new Uint8Array(await Bun.file(resolve(root, path)).arrayBuffer()))
      .digest("hex"),
  })),
)
await Bun.write(
  resolve(root, output, "build-manifest.json"),
  JSON.stringify(
    {
      BuiltUtc: new Date().toISOString(),
      Mode: windowsTakeover
        ? "local-lenovo-windows-takeover-12"
        : automatic
          ? "local-lenovo-dgpu-auto-unlock-11"
          : "local-lenovo-dgpu-unlock-10",
      RecoverAfterIsolationPolicyRefusal: true,
      ObservedConsoleRecovery: true,
      OwnerlessVirtualGopWithdrawal: true,
      SameVirtualInterfaceRestoration: true,
      HardwareTested: false,
      DriverStopStart: true,
      DirectGPUWrites: true,
      GPUReset: true,
      Unlock: true,
      SS0Only: true,
      PreserveSS1: true,
      MaximumUnlockTransactions: 1,
      PhysicalURequired: !automatic,
      AutomaticStart: automatic,
      WindowsDriverDisplayTakeover: windowsTakeover,
      RestoreFirmwareDisplayAfterUnlock: !windowsTakeover,
      WindowsHandoff: true,
      ReportDirectory: "\\RTX-UNLOCK",
      PE: pe,
      Sources: records,
    },
    null,
    2,
  ) + "\n",
)
console.log(JSON.stringify(pe))
