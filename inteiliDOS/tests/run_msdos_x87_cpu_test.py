#!/usr/bin/env python3
"""Software-only x87 CPU tests, sanitizer run, and freestanding i386 audit."""
from pathlib import Path
import argparse
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
SF=ROOT/"kernel/msdos/softfloat"
SOURCES=[ROOT/"kernel/msdos/tiny386/i386.c",
         ROOT/"kernel/msdos/tiny386/fpu.c"]+sorted(SF.glob("*.c"))+sorted((SF/"8086").glob("*.c"))
INCLUDES=["-I"+str(p) for p in (SF,SF/"include",SF/"8086")]

def run(*args,**kwargs):
    subprocess.run([str(a) for a in args],check=True,**kwargs)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize",action="store_true")
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="inteilidos-x87-") as folder:
        tmp=Path(folder)
        run("nasm","-f","bin",ROOT/"tests/msdos_x87.asm","-o",tmp/"x87.bin")
        flags=["-Os","-fno-strict-aliasing","-fwrapv","-DI386_ENABLE_FPU=1",*INCLUDES]
        if args.sanitize: flags+=["-g","-fsanitize=address,undefined","-fno-omit-frame-pointer"]
        run("cc",*flags,ROOT/"tests/msdos_x87_test.c",*SOURCES,"-o",tmp/"test")
        run(tmp/"test",tmp/"x87.bin")
        # Audit every generated instruction, not only the compiler flags.
        for i,src in enumerate(SOURCES):
            obj=tmp/f"{i}.o"
            run("cc",*flags[:4],*INCLUDES,"-m32","-march=i386","-msoft-float",
                "-mno-sse","-mno-mmx","-mno-sse2","-ffreestanding","-fno-builtin",
                "-fno-pic","-fno-pie","-c",src,"-o",obj)
            asm=subprocess.check_output(["objdump","-d","--no-show-raw-insn",str(obj)],text=True)
            for line in asm.splitlines():
                if ":\t" not in line: continue
                ins=line.split(":\t",1)[1].strip()
                mnemonic=ins.split()[0] if ins else ""
                if mnemonic.startswith("f") or "%xmm" in ins or "%mm" in ins:
                    raise AssertionError("Host floating-point/SIMD instruction: "+line)
        print("PASS: freestanding -march=i386 -msoft-float objects contain no x87/MMX/SSE instructions")

if __name__=="__main__":main()
