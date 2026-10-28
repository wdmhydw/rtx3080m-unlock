import assert from "node:assert/strict"
import { createHash } from "node:crypto"
import { resolve } from "node:path"
import Bun, { $ } from "bun"
import { inspectPe } from "./verify_pe"

const root = resolve(import.meta.dir, "..")
const output = "build/lenovo-dgpu-cycle"
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
  "src/unlock/runtime",
  "src/unlock/format",
]
const flags = [
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
  "-DGC_LENOVO_CYCLE",
  "-DGC_LENOVO_VIRTUAL_RELEASE",
  "-DGU_SSD_EXPERIMENT",
  "-DGC_WINDOWS_HANDOFF",
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
  "GfSavePci",
  "GwPrepare",
  "GwStart",
])
  assert(new RegExp(`\\b${symbol}\\b`).test(map), `Missing ${symbol}`)
for (const symbol of [
  "GfRun",
  "GxPatch",
  "GuFlr",
  "GfResetPci",
  "GuClaimController",
  "GuSingleOwnerRelease",
  "GxIndependentRun",
  "gpu_unlock_610_unlock",
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
    "src/gop/reset.h",
    "src/gop/windows.h",
    "src/gop/cycle.h",
    "src/unlock/boot_source.h",
    "tools/build-lenovo-cycle.ts",
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
      Mode: "local-lenovo-dgpu-isolation-9",
      RecoverAfterIsolationPolicyRefusal: true,
      ObservedConsoleRecovery: true,
      RecoveryOwnershipLogging: true,
      OwnerlessVirtualGopWithdrawal: true,
      SameVirtualInterfaceRestoration: true,
      HardwareTested: false,
      DriverStopStart: true,
      DirectGPUWrites: false,
      GPUReset: false,
      Unlock: false,
      PhysicalURequired: true,
      WindowsHandoff: true,
      ReportDirectory: "\\RTX-CYCLE",
      PE: pe,
      Sources: records,
    },
    null,
    2,
  ) + "\n",
)
console.log(JSON.stringify(pe))
