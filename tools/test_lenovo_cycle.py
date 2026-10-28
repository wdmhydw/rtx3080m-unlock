"""Exercise the actual lifecycle with the captured Lenovo firmware metadata."""
import json
import os
from pathlib import Path
import subprocess
import argparse

ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/"build"/"lenovo-cycle-check"
CRT=ROOT/"tools"/"simulation"/"crt"
LLVM=Path(os.environ["LLVM_BIN"])

def run(args):
    subprocess.run([str(x) for x in args],cwd=ROOT,check=True)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--legacy-source",type=Path)
    parser.add_argument("--previous-source",type=Path)
    parser.add_argument("--recovery-source",type=Path)
    parser.add_argument("--pci-fixture",type=Path,required=True,
                        help="Private, locally captured JSON containing a 64-word PciConfig; not distributed")
    args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    if args.legacy_source:
        (OUT/"legacy-cycle.c").write_bytes(args.legacy_source.read_bytes())
    if args.previous_source:
        (OUT/"previous-cycle.c").write_bytes(args.previous_source.read_bytes())
    if args.recovery_source:
        (OUT/"recovery-cycle.c").write_bytes(args.recovery_source.read_bytes())
    fixture=json.loads(args.pci_fixture.read_text(encoding="utf-8-sig"))
    config=fixture["PciConfig"]
    assert len(config)==64
    (OUT/"captured-config.inc").write_text(",\n".join(f"0x{v:08x}U" for v in config)+"\n",encoding="ascii")
    assembly=[".text"]
    for role,(start,stop) in enumerate([(0x1000,0x698),(0x10c8,0x1444),(0xcac,0xf34),(0x153c,0x1a90)]):
        assembly += [".p2align 4",f".globl TestImage{role}",f"TestImage{role}:"]
        callbacks=[(0x200,"Supported","MockSupported"),(start,"Start","MockStart"),(stop,"Stop","MockStop")]
        if role==3:callbacks += [(0x300,"GopQuery",None),(0x310,"GopSet",None),(0x320,"GopBlt",None)]
        for offset,name,target in sorted(callbacks):
            assembly += [f".org TestImage{role}+{offset}",f".globl Test{name}{role}",f"Test{name}{role}:",f"jmp {target}" if target else "ud2"]
    (OUT/"callbacks.s").write_text("\n".join(assembly)+"\n",encoding="ascii")
    app=(ROOT/"src/gop/app.c").read_bytes()
    gate=app[app.index(b"static EFI_STATUS Approve("):].split(b"static VOID Halt(")[0]
    (OUT/"approve-under-test.c").write_bytes(gate)
    clang,link,dlltool=[LLVM/(n+".exe") for n in ("clang","lld-link","llvm-dlltool")]
    flags=["-target","x86_64-pc-windows-msvc","-std=c11","-O1","-ffreestanding","-fno-builtin",
           "-fno-stack-protector","-mno-stack-arg-probe","-ffunction-sections","-Wall","-Wextra","-Werror",
           "-Wno-unused-parameter","-I",CRT,"-I","src/gop","-I","deps/edk2/MdePkg/Include","-I","deps/edk2/MdePkg/Include/X64"]
    run([dlltool,"-m","i386:x86-64","-d",CRT/"msvcrt.def","-l",OUT/"msvcrt.lib"])
    run([clang,*flags,"-c",CRT/"entry.c","-o",OUT/"entry.obj"])
    run([clang,"-target","x86_64-pc-windows-msvc","-c",OUT/"callbacks.s","-o",OUT/"callbacks.obj"])
    suites=[
        ("lifecycle",["src/gop/test_lenovo_cycle.c","src/gop/reset.c"],["-DGC_LENOVO_CYCLE","-DGC_LENOVO_VIRTUAL_RELEASE"]),
        ("approval",["src/gop/test_cycle_approval.c"],["-DGC_LENOVO_CYCLE","-DGC_LENOVO_VIRTUAL_RELEASE","-DGC_WINDOWS_HANDOFF","-DGU_SSD_EXPERIMENT"]),
        ("windows",["src/gop/test_windows.c"],[]),
        ("unlock",["src/gop/test_lenovo_unlock.c","src/gop/reset.c"],["-DGC_LENOVO_CYCLE","-DGC_LENOVO_VIRTUAL_RELEASE","-DGC_LENOVO_UNLOCK","-DGC_FULL_UNLOCK","-DGU_INDEPENDENT_DISPLAY"]),
        ("unlock-approval",["src/gop/test_cycle_approval.c"],["-DGC_LENOVO_CYCLE","-DGC_LENOVO_VIRTUAL_RELEASE","-DGC_LENOVO_UNLOCK","-DGC_FULL_UNLOCK","-DGC_WINDOWS_HANDOFF","-DGU_SSD_EXPERIMENT"]),
        ("unlock-auto",["src/gop/test_cycle_approval.c"],["-DGC_LENOVO_CYCLE","-DGC_LENOVO_UNLOCK","-DGC_LENOVO_AUTOSTART","-DGU_RUNTIME_PROFILE_BUILD"]),
        ("windows-takeover",["src/gop/test_windows_takeover.c","src/gop/reset.c"],["-DGC_LENOVO_CYCLE","-DGC_LENOVO_VIRTUAL_RELEASE","-DGC_LENOVO_UNLOCK","-DGC_FULL_UNLOCK","-DGU_INDEPENDENT_DISPLAY","-DGC_LENOVO_WINDOWS_TAKEOVER"]),
        ("takeover-auto",["src/gop/test_cycle_approval.c"],["-DGC_LENOVO_CYCLE","-DGC_LENOVO_UNLOCK","-DGC_LENOVO_AUTOSTART","-DGU_RUNTIME_PROFILE_BUILD","-DGC_LENOVO_WINDOWS_TAKEOVER"]),
    ]
    if args.legacy_source:
        suites.insert(0,("legacy",["src/gop/test_lenovo_cycle.c","src/gop/reset.c"],["-DLC_LEGACY_REPRO"]))
    if args.previous_source:
        suites.insert(0,("previous",["src/gop/test_lenovo_cycle.c","src/gop/reset.c"],["-DGC_LENOVO_CYCLE","-DLC_V7_REPRO"]))
    if args.recovery_source:
        suites.insert(0,("recovery",["src/gop/test_lenovo_cycle.c","src/gop/reset.c"],["-DGC_LENOVO_CYCLE","-DLC_V8_REPRO"]))
    for name,sources,extra in suites:
        objects=[]
        for index,source in enumerate(sources):
            obj=OUT/f"{name}-{index}.obj"
            run([clang,*flags,*extra,"-c",source,"-o",obj]);objects.append(obj)
        if name in ("lifecycle","legacy","previous","recovery","unlock","windows-takeover"):objects.append(OUT/"callbacks.obj")
        exe=OUT/(name+".exe")
        run([link,"/nodefaultlib","/entry:HostEntry","/subsystem:console","/opt:ref","/out:"+str(exe),*objects,OUT/"entry.obj",OUT/"msvcrt.lib"])
        result=subprocess.run([str(exe)],cwd=ROOT,capture_output=True,text=True)
        (OUT/(name+".log")).write_text(result.stdout+result.stderr,encoding="utf-8")
        print(result.stdout+result.stderr);result.check_returncode()

if __name__=="__main__":main()
