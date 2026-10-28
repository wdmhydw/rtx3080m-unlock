"""Run real tree/display/Windows code with isolated firmware service fixtures."""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "dgpu-diagnostic-check"
CRT = ROOT / "tools" / "simulation" / "crt"
LLVM = Path(os.environ["LLVM_BIN"])


def run(args):
    subprocess.run([str(x) for x in args], cwd=ROOT, check=True)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    clang, link, dlltool = [LLVM / (n + ".exe") for n in ("clang", "lld-link", "llvm-dlltool")]
    flags = ["-target", "x86_64-pc-windows-msvc", "-std=c11", "-O1", "-ffreestanding",
             "-fno-builtin", "-fno-stack-protector", "-mno-stack-arg-probe", "-ffunction-sections",
             "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-I", CRT,
             "-I", "deps/edk2/MdePkg/Include", "-I", "deps/edk2/MdePkg/Include/X64"]
    run([dlltool, "-m", "i386:x86-64", "-d", CRT / "msvcrt.def", "-l", OUT / "msvcrt.lib"])
    run([clang, *flags, "-c", CRT / "entry.c", "-o", OUT / "entry.obj"])
    for name, sources in [
        ("tree", ["src/gop/test_tree.c", "src/gop/tree.c"]),
        ("dgpu", ["src/gop/test_dgpu_diagnostic.c"]),
        ("windows", ["src/gop/test_windows.c"]),
    ]:
        objects = []
        for index, source in enumerate(sources):
            obj = OUT / f"{name}-{index}.obj"
            run([clang, *flags, "-c", source, "-o", obj])
            objects.append(obj)
        exe = OUT / (name + ".exe")
        run([link, "/nodefaultlib", "/entry:HostEntry", "/subsystem:console", "/opt:ref",
             "/out:" + str(exe), *objects, OUT / "entry.obj", OUT / "msvcrt.lib"])
        result = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, text=True)
        (OUT / (name + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
        print(result.stdout + result.stderr)
        result.check_returncode()


if __name__ == "__main__":
    main()
