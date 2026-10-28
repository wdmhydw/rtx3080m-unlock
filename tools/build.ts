import assert from "node:assert/strict"
import { resolve } from "node:path"
import Bun, { $ } from "bun"

const root = resolve(import.meta.dir, "..")
const preflightOnly =
  process.argv.length === 3 && process.argv[2] === "--preflight-only"
const autoStart =
  process.argv.length === 3 && process.argv[2] === "--lenovo-autostart"
const singleOwner =
  autoStart ||
  (process.argv.length === 3 && process.argv[2] === "--lenovo-single-owner")
if (process.argv.length > 2 && !preflightOnly && !singleOwner) {
  throw new Error(
    "Usage: bun tools/build.ts [--preflight-only | --lenovo-single-owner | --lenovo-autostart]",
  )
}
const output = preflightOnly
  ? "build/preflight-efi"
  : autoStart
    ? "build/lenovo-auto-efi"
    : singleOwner
      ? "build/lenovo-efi"
      : "build/efi"
const toolOptions = process.env.LLVM_BIN
  ? { PATH: process.env.LLVM_BIN }
  : undefined
const clang = Bun.which("clang", toolOptions)
const linker = Bun.which("lld-link", toolOptions)
if (!clang || !linker) {
  throw new Error("Install clang and lld-link and set PATH or LLVM_BIN.")
}

// Match the final FullUnlock/WindowsHandoff/Production/Ssd/External/Auto build.
const sources = [
  "src/gop/app",
  "src/gop/full",
  "src/unlock/runtime",
  "src/unlock/format",
  "src/gop/leaf",
  "src/gop/reset",
  "src/gop/visual",
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
  "src/probe/probe_app",
  "src/probe/probe_core",
  "src/probe/probe_profile",
  "src/unlock/profile_probe",
  "src/gop/windows",
  ...(preflightOnly ? ["src/gop/tree", "src/gop/owner_diagnostic"] : []),
  ...(singleOwner ? ["src/unlock/single_owner"] : []),
]
const flags = [
  ...(preflightOnly ? ["-DGC_PREFLIGHT_ONLY", "-ffunction-sections"] : []),
  ...(singleOwner ? ["-DGU_LENOVO_SINGLE_OWNER"] : []),
  ...(autoStart ? ["-DGU_LENOVO_AUTOSTART"] : []),
  "-target",
  "x86_64-pc-windows-msvc",
  "-std=c11",
  "-ffreestanding",
  "-fno-builtin",
  "-fno-stack-protector",
  "-mno-red-zone",
  "-fshort-wchar",
  "-Os",
  "-Wall",
  "-Wextra",
  "-Werror",
  "-DGC_FULL_FLR",
  "-DGU_INDEPENDENT_DISPLAY",
  "-DGC_FULL_UNLOCK",
  "-DGU_GOP_EXPERIMENT",
  "-D__KERNEL__",
  "-DGU_UEFI_PORT",
  "-DGU_RUNTIME_PROFILE_BUILD",
  "-DGU_AUTO_PROFILE",
  "-DGC_WINDOWS_HANDOFF",
  "-DGC_PRODUCTION",
  "-DGU_SSD_EXPERIMENT",
  "-Wno-unused-parameter",
  "-Wno-unused-function",
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
await $`${linker} /nologo /nodefaultlib /subsystem:efi_application /entry:efi_main /machine:x64 /base:0 /dynamicbase /nxcompat /timestamp:0 /opt:ref /opt:icf ${"/map:" + output + "/BOOTX64.map"} ${"/out:" + output + "/BOOTX64.EFI"} ${objects}`.cwd(
  root,
)
await $`${process.execPath} tools/verify_pe.ts ${output + "/BOOTX64.EFI"} ${output + "/BOOTX64.map"} --output ${output + "/pe-verification.json"}`.cwd(
  root,
)
const map = await Bun.file(resolve(root, output, "BOOTX64.map")).text()
for (const symbol of preflightOnly
  ? [
      "GuProfileCreate",
      "GuProfileRefresh",
      "GwPrepare",
      "GfPrepare",
      "GxPrepare",
      "GdCollectOwnerDiagnostic",
      "GcReadTree",
      "GuFindReleaseOwner",
    ]
  : [
      "GfRun",
      "GuProfileCreate",
      "GuProfileRefresh",
      "GuProfileCommit",
      "GwStart",
    ]) {
  assert(
    new RegExp(`\\b${symbol}\\b`).test(map),
    `Missing required code: ${symbol}`,
  )
}
if (preflightOnly) {
  for (const symbol of [
    "GfRun",
    "GwStart",
    "GuProfileCommit",
    "GxIndependentRun",
    "GxPatch",
    "gpu_unlock_610_unlock",
    "GuFlr",
  ]) {
    assert(
      !new RegExp(`\\b${symbol}\\b`).test(map),
      `Unexpected execution code: ${symbol}`,
    )
  }
}
if (singleOwner) {
  for (const symbol of [
    "GuSingleOwnerPrepare",
    "GuSingleOwnerRelease",
    "GxIndependentRun",
  ]) {
    assert(
      new RegExp(`\\b${symbol}\\b`).test(map),
      `Missing local owner code: ${symbol}`,
    )
  }
}
console.log(
  `Built ${preflightOnly ? "local-lenovo-owner-preflight-2 (unlock disabled)" : autoStart ? "local-lenovo-autostart-4 (automatic with unchanged profile)" : singleOwner ? "local-lenovo-single-owner-3 (physical U required)" : "v22-independent-ss0"}: ${resolve(root, output, "BOOTX64.EFI")}`,
)
