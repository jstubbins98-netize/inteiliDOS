#!/usr/bin/env python3
"""Exercise the real VBR with controlled BIOS E820 replies, not GRUB."""
from pathlib import Path
import struct
import subprocess
import tempfile
from build_test_kernel import ROOT

CASES = {
    "20-byte replies": (64, 48, 2, """
mov ecx,20
cmp byte [cs:0x5800],1
je more
xor ebx,ebx
jmp reply
more: mov ebx,1
"""),
    "disabled ACPI entry": (64, 24, 2, """
cmp byte [cs:0x5800],1
jne last
mov dword [es:di+20],0
mov ebx,1
jmp reply
last: xor ebx,ebx
"""),
    "invalid signature": (0, 0, 1, "xor eax,eax\nxor ebx,ebx\n"),
    "truncated entry": (0, 0, 1, "mov ecx,19\nxor ebx,ebx\n"),
    "failed continuation": (0, 0, 2, """
cmp byte [cs:0x5800],1
jne broken
mov ebx,1
jmp reply
broken: or word [ss:bp+6],1
"""),
    "endless disabled entries": (0, 0, 64, """
mov dword [es:di+20],0
mov ebx,1
"""),
}


def assemble(tmp, name, text):
    source=tmp/(name+".asm")
    source.write_text(text)
    output=tmp/(name+".bin")
    subprocess.run(["nasm","-f","bin",str(source),"-o",str(output)],check=True)
    return output.read_bytes()


def main():
    with tempfile.TemporaryDirectory(prefix="hdd-map-") as folder:
        tmp=Path(folder)
        subprocess.run(["nasm","-f","bin",str(ROOT/"boot/hdd_vbr.asm"),
                        "-o",str(tmp/"vbr.bin")],check=True)
        for name,(flags,length,calls,reply) in CASES.items():
            # Relocate the chainloader so BIOS can load the real VBR at 7C00.
            chain=assemble(tmp,"chain",f"""
bits 16
org 0x600
cli
xor ax,ax
mov ds,ax
mov es,ax
mov ss,ax
mov sp,0x7C00
cld
mov si,0x7C00
mov di,0x600
mov cx,256
rep movsw
jmp 0:entry
entry:
mov eax,[0x54]
mov [original],eax
mov word [0x54],handler
mov word [0x56],0
; Dirty handoff RAM must never be mistaken for a previous map.
mov di,0x5000
mov ax,0xcccc
mov cx,1024
rep stosw
mov byte [0x5800],0
sti
mov dl,0x80
mov si,dap
mov ah,0x42
int 0x13
jc $
mov dl,0x80
jmp 0:0x7C00
handler:
cmp eax,0xE820
jne other
push bp
mov bp,sp
and word [ss:bp+6],0xfffe
inc byte [cs:0x5800]
mov dword [es:di],0x100000
mov dword [es:di+4],0
mov dword [es:di+8],0x3f00000
mov dword [es:di+12],0
mov dword [es:di+16],1
mov dword [es:di+20],1
mov eax,0x534d4150
mov ecx,24
{reply}
reply:
pop bp
iret
other: jmp far [cs:original]
original: dd 0
align 4,db 0
dap: db 16,0
dw 1,0x7C00,0
dq 2048
times 510-($-$$) db 0
dw 0xaa55
""")
            kernel=assemble(tmp,"kernel",f"""
bits 32
org 0x100000
cmp eax,0x2badb002
jne fail
cmp ebx,0x5000
jne fail
cmp dword [0x5000],{flags}
jne fail
cmp dword [0x502c],{length}
jne fail
cmp byte [0x5800],{calls}
jne fail
mov al,0x10
jmp done
fail: mov al,0x11
done:
out 0xf4,al
cli
hlt
jmp $
times 4096-($-$$) db 0x5a
""")
            vbr=bytearray((tmp/"vbr.bin").read_bytes())
            struct.pack_into("<I",vbr,0x21,2049)
            struct.pack_into("<H",vbr,0x36,1)
            struct.pack_into("<I",vbr,0xDA,1024)
            disk=tmp/"target.img"
            with disk.open("wb") as f:
                f.truncate(8*1024*1024)
                f.write(chain);f.seek(2048*512);f.write(vbr);f.write(kernel)
            result=subprocess.run([
                "qemu-system-i386","-machine","pc","-cpu","486","-m","64",
                "-display","none","-serial","none","-monitor","none",
                "-no-reboot","-device","isa-debug-exit,iobase=0xf4,iosize=0x04",
                "-drive",f"file={disk},if=ide,format=raw","-boot","order=c,strict=on"],
                stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=20)
            assert result.returncode==33,(name,result.returncode,result.stderr.decode())
            print("PASS: BIOS map handoff:",name,flush=True)


if __name__=="__main__":
    main()
