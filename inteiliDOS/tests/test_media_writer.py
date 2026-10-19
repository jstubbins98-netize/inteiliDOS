#!/usr/bin/env python3
"""Safety tests with mocked host commands. Never opens or writes a disk."""
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import pty
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("writer",ROOT/"scripts/write_media.py")
w=importlib.util.module_from_spec(spec);spec.loader.exec_module(w)
DRIVE=dict(path="/dev/sdz",size=8000000,model="USB test",serial="test serial",mounts=[])

class WriterTests(unittest.TestCase):
    def test_linux_filters_system_readonly_partitions(self):
        disks=[
            dict(path="/dev/sda",type="disk",size=999,tran="usb",ro=False,children=[
                dict(path="/dev/sda1",mountpoints=["/"])]),
            dict(path="/dev/sdb",type="disk",size=8000000,tran="usb",ro=False),
            dict(path="/dev/sdc",type="disk",size=999,tran="usb",ro=True),
            dict(path="/dev/sdd",type="part",size=999,tran="usb",ro=False)]
        with patch.object(w,"output",return_value=json.dumps({"blockdevices":disks})):
            self.assertEqual([d["path"] for d in w.linux_devices("usb")],["/dev/sdb"])
        disks[0]["children"][0]["mountpoints"]=[]
        with patch.object(w,"output",return_value=json.dumps({"blockdevices":disks})):
            with self.assertRaises(RuntimeError):w.linux_devices("usb")

    def test_mac_external_apfs_boot_disk_excluded(self):
        def out(args):
            if args[:3]==["diskutil","info","-plist"] and args[3]=="/":
                result=dict(ParentWholeDisk="disk3",DeviceIdentifier="disk3s1s1",FilesystemType="apfs")
            elif args[1]=="apfs":
                result={"Containers":[
                    dict(ContainerReference="disk3",PhysicalStores=[dict(DeviceIdentifier="disk2s2")]),
                    dict(ContainerReference="disk5",PhysicalStores=[dict(DeviceIdentifier="disk4s2")],
                         Volumes=[dict(DeviceIdentifier="disk5s1",MountPoint="/Volumes/USB")])]}
            elif args[1]=="list":
                result={"AllDisksAndPartitions":[dict(DeviceIdentifier="disk2"),dict(DeviceIdentifier="disk4")]}
            else:
                result=dict(Internal=False,Whole=True,BusProtocol="USB",TotalSize=8000000)
            return plistlib.dumps(result)
        with patch.object(w,"output",side_effect=out):
            devices=w.mac_devices("usb")
            self.assertEqual([d["path"] for d in devices],["/dev/disk4"])
            self.assertEqual(devices[0]["mounts"],[("disk5s1","/Volumes/USB")])

    def exercise_flash(self,reply,*,mounted=False,fail=None,small=False,source=False):
        with tempfile.TemporaryDirectory() as tmp:
            image=Path(tmp)/"image.iso";image.write_bytes(b"x"*1024)
            device=dict(DRIVE)
            if small:device["size"]=512
            if mounted or source:device["mounts"]=[("/dev/sdz1",tmp if source else "/media/usb")]
            clean=dict(device,mounts=[])
            calls=[]
            def run(args,**kwargs):
                calls.append(args)
                if fail and args[0]==fail:raise subprocess.CalledProcessError(1,args)
            with patch.object(w,"devices",side_effect=[[device],[device],[clean]]), \
                 patch.object(w,"is_device",return_value=True), \
                 patch.object(w.shutil,"which",return_value="/test/tool"), \
                 patch.object(w.os,"geteuid",return_value=0), \
                 patch.object(w,"run",side_effect=run):
                if small or source or fail:
                    with self.assertRaises((RuntimeError,subprocess.CalledProcessError)):
                        w.flash(image,device,"usb",lambda _:reply)
                else:
                    self.assertEqual(w.flash(image,device,"usb",lambda _:reply),
                                     reply=="ERASE /dev/sdz")
            return calls

    def test_cancel_never_unmounts_or_writes(self):
        self.assertEqual(self.exercise_flash("yes",mounted=True),[])

    def test_explicit_confirmation_and_readback(self):
        calls=self.exercise_flash("ERASE /dev/sdz",mounted=True)
        self.assertEqual([a[0] for a in calls],["umount","dd","sync",w.sys.executable])
        self.assertIn("of=/dev/sdz",calls[1]);self.assertIn("conv=fsync",calls[1])
        self.assertIn("Read-back verification failed",calls[-1][2])

    def test_image_on_target_and_capacity_rejected(self):
        self.assertEqual(self.exercise_flash("ERASE /dev/sdz",source=True),[])
        self.assertEqual(self.exercise_flash("ERASE /dev/sdz",small=True),[])

    def test_unmount_or_write_failure_stops(self):
        calls=self.exercise_flash("ERASE /dev/sdz",mounted=True,fail="umount")
        self.assertEqual(len(calls),1)
        calls=self.exercise_flash("ERASE /dev/sdz",fail="dd")
        self.assertEqual(len(calls),1)

    def test_changed_device_not_used(self):
        with patch.object(w,"devices",return_value=[dict(DRIVE,serial="different")]):
            with self.assertRaises(RuntimeError):w.current(DRIVE,"usb")

    def test_verification_failure_is_not_success(self):
        calls=self.exercise_flash("ERASE /dev/sdz",fail=w.sys.executable)
        self.assertEqual([a[0] for a in calls],["dd","sync",w.sys.executable])

    def test_noninteractive_writer_refused(self):
        with patch.object(w.sys,"argv",["writer","--kind","usb","--image","unused.iso"]), \
             patch.object(w.sys.stdin,"isatty",return_value=False), \
             patch.object(w,"devices") as enumerate_devices:
            with self.assertRaises(RuntimeError):w.main()
            enumerate_devices.assert_not_called()

    def test_mac_burn_uses_explicit_optical_drive(self):
        replies=iter(["2","BURN 2"])
        with patch.object(w.sys,"platform","darwin"), \
             patch.object(w.shutil,"which",return_value="/usr/bin/drutil"), \
             patch.object(w,"output",return_value=b"Vendor Product Rev Bus\n1: TEST CD-R 1.0 USB Supported\n2: TEST DVD-R 1.0 USB Supported\n"), \
             patch.object(w,"run") as run:
            self.assertTrue(w.burn(Path("/tmp/test.iso"),lambda _:next(replies)))
            run.assert_called_once_with(["drutil","-drive","2","burn","-image","/tmp/test.iso"])

    def test_prompt_iso_optical_usb_and_floppy(self):
        master,slave=pty.openpty()
        script=f"""
set -euo pipefail
USE_DEFAULTS=0; BOOT_MEDIA=3; BUILD_DIR='/some path'; SCRIPT_DIR='{ROOT}'
willburd() {{ echo "$*"; }}; warn() {{ echo "$*"; }}; info() {{ echo "$*"; }}
python3() {{ echo "WRITE:$*"; }}
source '{ROOT}/scripts/media_prompts.sh'
offer_media_writing
"""
        try:
            p=subprocess.Popen(["bash","-c",script],stdin=slave,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            os.write(master,b"I\ny\ny\nF\ny\nN\n")
            text=p.communicate(timeout=10)[0].decode()
            self.assertEqual(p.returncode,0,text)
            self.assertEqual(text.count("WRITE:"),3,text)
            for kind in ("optical","usb","floppy"):self.assertIn("--kind "+kind,text)
        finally:os.close(master);os.close(slave)

if __name__=="__main__":unittest.main()
