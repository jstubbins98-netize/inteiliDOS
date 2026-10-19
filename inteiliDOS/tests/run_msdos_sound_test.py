#!/usr/bin/env python3
"""Run real DOS PCM/IRQ fixtures and verify native AC97/HDA WAV output."""
from pathlib import Path
import argparse
import array
import subprocess
import tempfile
import wave
from build_test_kernel import ROOT, run
from run_launchpad_dos_test import Guest, fat12_image
from run_msdos622_test import prepare, shell, clear_dos
from run_msdos_external_media_test import native_disk, protected


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernel",type=Path)
    parser.add_argument("backend",nargs="?",choices=("ac97","hda","none"),default="ac97")
    parser.add_argument("--disabled",action="store_true")
    parser.add_argument("--hda-controller",choices=("intel-hda","ich9-intel-hda"),
                        default="ich9-intel-hda")
    parser.add_argument("--hda-codec",choices=("hda-output","hda-duplex","hda-micro"),
                        default="hda-duplex")
    options=parser.parse_args()
    kernel=options.kernel.resolve()
    backend,disabled=options.backend,options.disabled
    tag=backend
    if backend=="hda" and (options.hda_controller!="ich9-intel-hda" or
                           options.hda_codec!="hda-duplex"):
        tag+=f"-{options.hda_controller}-{options.hda_codec}"
    if disabled: tag+="-disabled"
    out=ROOT.parent/"generated-artifacts/msdos-sound"
    out.mkdir(parents=True,exist_ok=True)
    wav=out/(tag+".wav")
    with tempfile.TemporaryDirectory(prefix="dos-sound-") as folder:
        tmp=Path(folder)
        prepare(ROOT.parent/"MS-DOS_disks",tmp/"disc")
        for name,flags in (("SOUND",()),("AUTO",("-DAUTO=1",))):
            run("nasm","-f","bin",*flags,ROOT/"tests/msdos_sound.asm","-o",tmp/(name+".com"))
        (tmp/"disc/SOUND.IMG").write_bytes(fat12_image({
            "SOUND.COM":(tmp/"SOUND.com").read_bytes(),
            "AUTO.COM":(tmp/"AUTO.com").read_bytes(),
        }))
        run("xorriso","-as","mkisofs","-quiet","-o",tmp/"dos.iso",tmp/"disc")
        native=tmp/"native.img";native_disk(native);safe=protected(native)
        args=["-drive",f"file={native},if=ide,index=1,format=raw"]
        if backend!="none":
            args+=["-audiodev",f"wav,id=audio,path={wav},out.frequency=48000"]
            args+=["-device","AC97,audiodev=audio"] if backend=="ac97" else [
                "-device",options.hda_controller,
                "-device",options.hda_codec+",audiodev=audio"]
        guest=Guest(kernel,tmp/"dos.iso",False,tmp,tuple(args))
        symbols=subprocess.check_output(["nm",str(kernel)],text=True)
        locations={p[2]:int(p[0],16) for line in symbols.splitlines()
                   if len(p:=line.split())==3}
        saved=tmp/"registers.bin"

        def physical(address,size=4):
            guest.command("pmemsave",{"val":address,"size":size,"filename":str(saved)})
            return saved.read_bytes()

        def symbol(name,size=4):
            return int.from_bytes(physical(locations[name],size),"little")

        def check_output():
            if disabled:
                assert "backend" not in locations,"Disabled build contains PCI audio state"
                return 0
            actual=symbol("backend")
            if backend=="none":
                assert actual==0,("Unexpected native output",actual)
                return 0
            if actual!=(1 if backend=="ac97" else 2):
                reason=physical(symbol("failure"),80).split(b"\0")[0].decode(errors="replace")
                raise AssertionError("Native audio unavailable: "+reason)
            if backend=="hda":
                base=symbol("mmio")
                registers=physical(base,0x400)
                # Response status events are needed even though delivery of
                # all native HDA interrupts stays masked.
                assert int.from_bytes(registers[0x20:0x24],"little")==0
                assert registers[0x5c]&3==3
                assert registers[0x4c]&2
                assert registers[symbol("stream")]&0x1e==2
                return base
            return 0

        def check_stopped(base):
            if disabled: return
            assert symbol("backend")==0,"Native audio backend survived F8"
            # Restoring PCI memory decoding to off also hides the registers.
            if base and symbol("saved_command",2)&2:
                registers=physical(base,0x400)
                assert not registers[symbol("stream")]&2,"Output DMA still running"
                assert not registers[0x4c]&2,"CORB DMA still running"
                assert not registers[0x5c]&2,"RIRB DMA still running"

        try:
            guest.wait("Welcome to inteiliDOS");guest.wait("> ")
            shell(guest,"ms-dos")
            guest.wait("MS-DOS 6.22 -",180);guest.wait("C:\\>")
            base=check_output()
            guest.key("f8");guest.wait("Returned from MS-DOS")
            check_stopped(base)
            shell(guest,"ms-dos /image SOUND.IMG")
            guest.wait("MS-DOS 6.22 -",90);guest.wait("C:\\>")
            base=check_output()
            clear_dos(guest);shell(guest,"a:\\sound.com")
            guest.wait("NO DSP DETECTED" if disabled else "SOUND IRQ OK",30)
            if not disabled:
                clear_dos(guest);shell(guest,"a:\\auto.com")
                guest.wait("SOUND IRQ OK",30)
                # Abort active playback and re-enter: no stale DMA/IRQ state.
                clear_dos(guest);shell(guest,"a:\\auto.com")
                guest.wait("DSP DETECTED")
            guest.key("f8");guest.wait("Returned from MS-DOS")
            check_stopped(base)
            if not disabled and backend!="none":
                guest.wait("DOS audio output stopped:")
            guest.command("screendump",{"filename":str(out/(tag+"-shell.ppm"))})
            shell(guest,"ms-dos /image SOUND.IMG")
            guest.wait("MS-DOS 6.22 -",90);guest.wait("C:\\>")
            base=check_output()
            clear_dos(guest);shell(guest,"a:\\sound.com")
            guest.wait("NO DSP DETECTED" if disabled else "SOUND IRQ OK",30)
            guest.key("f8");guest.wait("Returned from MS-DOS")
            check_stopped(base)
        except Exception:
            # Capture private CPU/device state for hardware-backend failures.
            symbols=subprocess.check_output(["nm",str(kernel)],text=True)
            for line in symbols.splitlines():
                parts=line.split()
                if len(parts)==3 and parts[2] in ("s","mem","regs16","backend","mmio","stream","corb","rirb"):
                    address=int(parts[0],16)
                    path=out/(tag+"-"+parts[2]+".bin")
                    size={"s":80,"corb":1024,"rirb":2048}.get(parts[2],4)
                    guest.command("pmemsave",{"val":address,"size":size,
                                              "filename":str(path)})
            raise
        finally:
            # The shared Guest.close kills QEMU; WAV headers need a clean quit.
            if guest.proc.poll() is None:
                guest.command("quit")
                guest.proc.wait(timeout=5)
            guest.sock.close()
        assert protected(native)==safe
    if backend!="none" and (not disabled or wav.stat().st_size):
        with wave.open(str(wav),"rb") as f:
            assert f.getsampwidth()==2 and f.getnchannels()==2
            rate=f.getframerate()
            values=array.array("h",f.readframes(f.getnframes()))[::2]
        if disabled:
            assert all(abs(x)<8 for x in values),"Disabled device emitted sound"
        else:
            loud=[x for x in values if abs(x)>1000]
            assert len(loud)>rate*4,("Missing recorded PCM",len(loud),len(values))
            # Guest generated an eight-sample period at 8 kHz => 1 kHz.
            start=next(i for i,x in enumerate(values) if abs(x)>1000)+rate//4
            segment=values[start:start+rate//2]
            crossings=sum(a<=0<b for a,b in zip(segment,segment[1:]))
            assert 450<crossings<550,("Wrong PCM frequency",crossings)
            print(f"PASS: {tag} recorded digital PCM, ~{crossings*2} Hz, "
                  f"{len(loud)/rate:.1f}s non-silent; single/auto-init guest IRQ5, F8, re-entry")
            if backend=="hda":
                print("PASS: native IRQs masked; stream/CORB/RIRB stopped on every F8")
    elif disabled:
        print("PASS: disabled DSP absent; no native audio output")
    else:
        print("PASS: no-output fallback remains responsive with guest DMA/IRQ completion")
    print("PASS: config on/off behavior and native disk preservation")


if __name__=="__main__":
    main()
