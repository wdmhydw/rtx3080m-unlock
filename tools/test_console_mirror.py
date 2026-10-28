"""Compile the actual display check against modeled EFI services on Windows."""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "console-check"
CRT = ROOT / "tools" / "simulation" / "crt"
LLVM = Path(os.environ["LLVM_BIN"])


def run(args):
    subprocess.run([str(x) for x in args], cwd=ROOT, check=True)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    original = subprocess.check_output(["git", "show", "HEAD:src/unlock/app.c"], cwd=ROOT)
    # Keep the exact production includes, DisplayCheck and its helper functions.
    # Unrelated hardware transaction routines are excluded from this host unit.
    for name, source in [("original", original), ("patched", (ROOT / "src/unlock/app.c").read_bytes())]:
        boundary = b"static EFI_STATUS ReadConfig("
        if source.count(boundary) != 1:
            raise ValueError("Unexpected production display-check boundary")
        (OUT / (name + "-app.c")).write_bytes(source.split(boundary)[0])
    clang, link, dlltool = [LLVM / (name + ".exe") for name in ("clang", "lld-link", "llvm-dlltool")]
    flags = ["-target", "x86_64-pc-windows-msvc", "-std=c11", "-O1", "-ffreestanding",
             "-fno-builtin", "-fno-stack-protector", "-mno-stack-arg-probe", "-ffunction-sections",
             "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-function",
             "-D__KERNEL__", "-DGU_UEFI_PORT", "-DGU_GOP_EXPERIMENT", "-DGU_RUNTIME_PROFILE_BUILD",
             "-DGU_INDEPENDENT_DISPLAY", "-DGU_SSD_EXPERIMENT", "-DGC_FULL_UNLOCK",
             "-I", CRT, "-I", "src/unlock", "-I", "src/unlock/compat",
             "-I", "deps/edk2/MdePkg/Include", "-I", "deps/edk2/MdePkg/Include/X64"]
    run([dlltool, "-m", "i386:x86-64", "-d", CRT / "msvcrt.def", "-l", OUT / "msvcrt.lib"])
    run([clang, *flags, "-c", CRT / "entry.c", "-o", OUT / "entry.obj"])
    run([clang, *flags, "-c", "src/unlock/checks.c", "-o", OUT / "checks.obj"])
    for name, defines in [("original", ["-DTEST_ORIGINAL"]), ("patched", [])]:
        obj, exe = OUT / (name + ".obj"), OUT / (name + ".exe")
        run([clang, *flags, *defines, "-c", "src/unlock/tests/test_console_mirror.c", "-o", obj])
        run([link, "/nodefaultlib", "/entry:HostEntry", "/subsystem:console", "/opt:ref",
             "/out:" + str(exe), obj, OUT / "checks.obj", OUT / "entry.obj", OUT / "msvcrt.lib"])
        result = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, text=True)
        (OUT / (name + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
        print(result.stdout + result.stderr)
        result.check_returncode()


if __name__ == "__main__":
    main()
