"""Run production ownership and independent flow with simulated firmware services."""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "single-owner-check"
CRT = ROOT / "tools" / "simulation" / "crt"
LLVM = Path(os.environ["LLVM_BIN"])


def run(args):
    subprocess.run([str(x) for x in args], cwd=ROOT, check=True)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    display = ROOT / "build" / "console-check"
    display.mkdir(parents=True, exist_ok=True)
    app = (ROOT / "src/unlock/app.c").read_bytes()
    (display / "patched-app.c").write_bytes(app.split(b"static EFI_STATUS ReadConfig(")[0])
    flow = app[app.index(b"static BOOLEAN IndependentIsolated("):]
    end = b"#else\r\nEFI_STATUS EFIAPI efi_main("
    if flow.count(end) != 1:
        raise ValueError("Unexpected independent flow boundary")
    (OUT / "flow-under-test.c").write_bytes(flow.split(end)[0])
    port = (ROOT / "src/unlock/port.c").read_bytes()
    target = port[port.index(b"BOOLEAN GuTargetConfig("):].split(b"BOOLEAN GuSafeToReturn(")[0]
    (OUT / "target-under-test.c").write_bytes(target)
    gop = (ROOT / "src/gop/app.c").read_bytes()
    gate = gop[gop.index(b"static EFI_STATUS Approve("):].split(b"static VOID Halt(")[0]
    (OUT / "approve-under-test.c").write_bytes(gate)
    clang, link, dlltool = [LLVM / (name + ".exe") for name in ("clang", "lld-link", "llvm-dlltool")]
    flags = ["-target", "x86_64-pc-windows-msvc", "-std=c11", "-O1", "-ffreestanding",
             "-fno-builtin", "-fno-stack-protector", "-mno-stack-arg-probe", "-ffunction-sections",
             "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-function",
             "-D__KERNEL__", "-DGU_UEFI_PORT", "-DGU_GOP_EXPERIMENT", "-DGU_RUNTIME_PROFILE_BUILD",
             "-DGU_INDEPENDENT_DISPLAY", "-DGU_SSD_EXPERIMENT", "-DGC_FULL_UNLOCK",
             "-DGU_LENOVO_SINGLE_OWNER", "-I", CRT, "-I", "src/unlock", "-I", "src/unlock/compat",
             "-I", "deps/edk2/MdePkg/Include", "-I", "deps/edk2/MdePkg/Include/X64"]
    run([dlltool, "-m", "i386:x86-64", "-d", CRT / "msvcrt.def", "-l", OUT / "msvcrt.lib"])
    sources = [CRT / "entry.c", "src/unlock/tests/test_single_owner.c", "src/unlock/single_owner.c",
               "src/unlock/ownership.c", "src/unlock/checks.c"]
    objects = []
    for index, source in enumerate(sources):
        obj = OUT / f"part-{index}.obj"
        run([clang, *flags, "-c", source, "-o", obj])
        objects.append(obj)
    exe = OUT / "single-owner.exe"
    run([link, "/nodefaultlib", "/entry:HostEntry", "/subsystem:console", "/opt:ref",
         "/out:" + str(exe), *objects, OUT / "msvcrt.lib"])
    result = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, text=True)
    (OUT / "single-owner.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    print(result.stdout + result.stderr)
    result.check_returncode()
    gate_obj, gate_exe = OUT / "gate.obj", OUT / "gate.exe"
    gate_flags = ["-DGC_PRODUCTION", "-DGC_FULL_FLR", "-DGC_WINDOWS_HANDOFF", "-DGU_AUTO_PROFILE"]
    run([clang, *flags, *gate_flags, "-c", "src/gop/test_local_approval.c", "-o", gate_obj])
    run([link, "/nodefaultlib", "/entry:HostEntry", "/subsystem:console", "/opt:ref",
         "/out:" + str(gate_exe), gate_obj, objects[0], OUT / "msvcrt.lib"])
    gate_result = subprocess.run([str(gate_exe)], cwd=ROOT, capture_output=True, text=True)
    (OUT / "gate.log").write_text(gate_result.stdout + gate_result.stderr, encoding="utf-8")
    print(gate_result.stdout + gate_result.stderr)
    gate_result.check_returncode()
    auto_obj, auto_exe = OUT / "auto-gate.obj", OUT / "auto-gate.exe"
    run([clang, *flags, *gate_flags, "-DGU_LENOVO_AUTOSTART", "-c",
         "src/gop/test_auto_approval.c", "-o", auto_obj])
    run([link, "/nodefaultlib", "/entry:HostEntry", "/subsystem:console", "/opt:ref",
         "/out:" + str(auto_exe), auto_obj, objects[0], OUT / "msvcrt.lib"])
    auto_result = subprocess.run([str(auto_exe)], cwd=ROOT, capture_output=True, text=True)
    (OUT / "auto-gate.log").write_text(auto_result.stdout + auto_result.stderr, encoding="utf-8")
    print(auto_result.stdout + auto_result.stderr)
    auto_result.check_returncode()


if __name__ == "__main__":
    main()
