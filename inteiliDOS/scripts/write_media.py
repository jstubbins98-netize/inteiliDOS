#!/usr/bin/env python3
"""Optional local-host media writer. Never called by unattended builds.

No disk is chosen automatically. Refuse system disks, partitions, read-only
devices, undersized devices and image files stored on the selected target.
All subprocess arguments are passed without a shell.
"""
import argparse
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import stat
import subprocess
import sys


def run(args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def output(args):
    return subprocess.check_output(args)


def children(node):
    yield node
    for child in node.get("children", []):
        yield from children(child)


def mounts(node):
    return [p for p in node.get("mountpoints", []) if p]


def protected_mount(p):
    return p in ("/", "/home", "/usr", "/var", "[SWAP]") or p.startswith(("/boot", "/home/"))


def linux_devices(kind):
    tree = json.loads(output(["lsblk", "--json", "--bytes", "--output",
        "PATH,TYPE,SIZE,RM,RO,TRAN,MODEL,SERIAL,MOUNTPOINTS"]))["blockdevices"]
    if not any("/" in mounts(n) for disk in tree for n in children(disk)):
        raise RuntimeError("Cannot identify the system/root disk; refusing raw writes.")
    choices = []
    for disk in tree:
        nodes = list(children(disk))
        if any(protected_mount(p) for n in nodes for p in mounts(n)):
            continue
        path = disk["path"]
        if kind == "optical":
            eligible = disk["type"] == "rom"
        else:
            eligible = disk["type"] == "disk" and not disk.get("ro")
            if kind == "usb":
                eligible &= disk.get("tran") == "usb"
            else:
                eligible &= bool(re.fullmatch(r"/dev/fd\d+", path) or
                    (disk.get("tran") == "usb" and int(disk["size"]) <= 64*1024*1024))
        if eligible:
            choices.append(dict(path=path, size=int(disk["size"]),
                model=(disk.get("model") or "").strip(), serial=disk.get("serial") or "",
                mounts=[(n["path"], p) for n in nodes for p in mounts(n)]))
    return choices


def mac_devices(kind):
    def plist(args):
        return plistlib.loads(output(args))
    root = plist(["diskutil", "info", "-plist", "/"])
    root_id = root.get("ParentWholeDisk", root["DeviceIdentifier"])
    protected = {re.match(r"disk\d+", root_id)[0]}
    apfs = {"Containers": []}
    if root.get("FilesystemType", "").lower() == "apfs" or root.get("APFSContainerReference"):
        apfs = plist(["diskutil", "apfs", "list", "-plist"])
        container = next((c for c in apfs["Containers"]
                          if c["ContainerReference"] in protected), None)
        if not container:
            raise RuntimeError("Cannot identify the APFS system disk; refusing raw writes.")
        protected.update(re.match(r"disk\d+", p["DeviceIdentifier"])[0]
                         for p in container["PhysicalStores"])
    listing = plist(["diskutil", "list", "-plist", "external", "physical"])
    choices = []
    for disk in listing.get("AllDisksAndPartitions", []):
        name = disk["DeviceIdentifier"]
        if name in protected or not re.fullmatch(r"disk\d+", name):
            continue
        info = plist(["diskutil", "info", "-plist", "/dev/"+name])
        if info.get("Internal", True) or not info.get("Whole", False):
            continue
        if info.get("MediaReadOnly", False) or info.get("Writable") is False:
            continue
        if info.get("BusProtocol", info.get("Protocol")) != "USB":
            continue
        size = int(info["TotalSize"])
        if kind == "floppy" and size > 64*1024*1024:
            continue
        mounted = [(n["DeviceIdentifier"], n["MountPoint"])
                   for n in [disk, *disk.get("Partitions", [])] if n.get("MountPoint")]
        # APFS volumes belong to a synthesized container, not to the physical
        # disk's partition listing. Include them when protecting source images.
        if not apfs["Containers"]:
            try:
                apfs = plist(["diskutil", "apfs", "list", "-plist"])
            except subprocess.CalledProcessError as exc:
                raise RuntimeError("Cannot check external APFS mounts; refusing raw writes.") from exc
        for container in apfs["Containers"]:
            if any(re.match(r"disk\d+", p["DeviceIdentifier"])[0] == name
                   for p in container["PhysicalStores"]):
                mounted += [(v["DeviceIdentifier"], v["MountPoint"])
                            for v in container.get("Volumes", []) if v.get("MountPoint")]
        choices.append(dict(path="/dev/"+name, size=size,
            model=info.get("MediaName", ""), serial=info.get("DeviceTreePath", ""),
            mounts=mounted))
    return choices


def devices(kind):
    if sys.platform == "darwin":
        return mac_devices(kind)
    if sys.platform.startswith("linux"):
        return linux_devices(kind)
    raise RuntimeError("Automatic media writing supports macOS and Linux only.")


def identity(device):
    return tuple(device[k] for k in ("path", "size", "model", "serial"))


def current(device, kind):
    found = next((d for d in devices(kind) if
                  (identity(d) == identity(device) if kind != "optical" else
                   (d["path"], d["model"], d["serial"]) ==
                   (device["path"], device["model"], device["serial"]))), None)
    if found is None:
        raise RuntimeError("The selected device changed/disappeared. Nothing written.")
    return found


def privileged(args):
    if os.geteuid() != 0:
        if not shutil.which("sudo"):
            raise RuntimeError("sudo is needed to access the selected physical device.")
        return ["sudo", *args]
    return args


def source_on_target(image, device):
    return any(image == Path(p) or Path(p) in image.parents for _, p in device["mounts"])

def is_device(path):
    mode = os.stat(path).st_mode
    return stat.S_ISBLK(mode) or (sys.platform == "darwin" and stat.S_ISCHR(mode))

VERIFY = """import sys
with open(sys.argv[1], 'rb') as image, open(sys.argv[2], 'rb') as target:
    while True:
        expected = image.read(1048576)
        if not expected:
            break
        if target.read(len(expected)) != expected:
            raise SystemExit('Read-back verification failed; media is incomplete or damaged.')
"""

def flash(image, device, kind, confirm=input):
    device = current(device, kind)
    if image.stat().st_size > device["size"]:
        raise RuntimeError("The image is larger than the selected drive.")
    if source_on_target(image, device):
        raise RuntimeError("The image is stored on the selected drive. Copy it elsewhere first.")
    if not is_device(device["path"]):
        raise RuntimeError("Target is not a physical disk device.")
    for tool in ("dd", "sync"):
        if not shutil.which(tool):
            raise RuntimeError(tool+" is required; no media written.")
    # Check privilege availability before asking to erase or unmounting.
    privileged(["dd"])
    print("\nERASE WARNING: ALL existing data/partitions on", device["path"], "will be lost.")
    if kind == "usb":
        print("A raw ISO replaces FAT/exFAT with its image layout. It is not a file copy.")
        print("Firmware bootability depends on the ISO; UEFI-only boot is not supported.")
    phrase = "ERASE "+device["path"]
    if confirm("Type '"+phrase+"' to proceed (anything else cancels): ").strip() != phrase:
        print("Cancelled; no media changed.")
        return False
    device = current(device, kind)
    if source_on_target(image, device):
        raise RuntimeError("Source is on target; no media written.")
    if sys.platform == "darwin":
        run(["diskutil", "unmountDisk", device["path"]])
    else:
        # Child partitions first; never unmount anything outside this drive.
        for path, _ in reversed(device["mounts"]):
            run(privileged(["umount", path]))
    device = current(device, kind)
    if device["mounts"] or not is_device(device["path"]):
        raise RuntimeError("Target still has mounted filesystems; refusing to write.")
    target = device["path"].replace("/dev/disk", "/dev/rdisk") if sys.platform == "darwin" else device["path"]
    command = ["dd", "if="+str(image), "of="+target, "bs=1048576"]
    if sys.platform.startswith("linux"):
        command.append("conv=fsync")
    run(privileged(command))
    run(["sync"])
    run(privileged([sys.executable, "-c", VERIFY, str(image), target]))
    print("Image written and read-back verified. Safely eject using your host OS before removal.")
    return True


def burn(image, confirm=input):
    if sys.platform == "darwin":
        if not shutil.which("drutil"):
            raise RuntimeError("macOS drutil is required.")
        listing = output(["drutil", "list"]).decode(errors="replace")
        print(listing)
        indices = set()
        for line in listing.splitlines():
            match = re.match(r"^\s*(\d+)\s*[:.]?\s+(.+)$", line)
            if match and len(match[2].split()) >= 3:
                indices.add(match[1])
        if not indices:
            raise RuntimeError("No identifiable optical recorders; no media written.")
        choice = confirm("Choose the optical drive NUMBER shown above, or Enter to cancel: ").strip()
        if not choice:
            return False
        if choice not in indices:
            raise RuntimeError("Invalid optical drive selection; no media written.")
        print("Insert a blank disc in optical drive", choice)
        phrase = "BURN "+choice
        if confirm("Type '"+phrase+"' to burn this disc: ").strip() != phrase:
            print("Cancelled; no media changed.")
            return False
        # -image writes the ISO itself; verification is enabled by default.
        # Do not pass -erase: an existing written disc must not be auto-erased.
        run(["drutil", "-drive", choice, "burn", "-image", str(image)])
    else:
        print("Insert a blank CD/DVD before choosing the optical recorder.")
        options = devices("optical")
        device = select(options, confirm)
        if device is None:
            return False
        if not shutil.which("xorriso"):
            raise RuntimeError("xorriso is required for optical recording.")
        print("Insert a blank CD/DVD in", device["path"], "(existing written discs are not auto-erased).")
        phrase = "BURN "+device["path"]
        if confirm("Type '"+phrase+"' to continue: ").strip() != phrase:
            print("Cancelled; no media changed.")
            return False
        device = current(device, "optical")
        for path, _ in reversed(device["mounts"]):
            run(privileged(["umount", path]))
        run(privileged(["xorriso", "-as", "cdrecord", "-v",
                        "dev="+device["path"], str(image)]))
    print("Optical recording completed successfully.")
    return True


def select(options, confirm=input):
    if not options:
        print("No eligible drives found. Connect/insert suitable media and try again.")
        return None
    for i, d in enumerate(options, 1):
        print(f"{i}. {d['path']}  {d['size']//1024} KiB  {d['model']}  {d['serial']}")
    reply = confirm("Choose a drive NUMBER, or Enter to cancel: ").strip()
    if not reply:
        return None
    if not reply.isdecimal() or not 1 <= int(reply) <= len(options):
        raise RuntimeError("Invalid drive selection; no media changed.")
    return options[int(reply)-1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kind", choices=("usb", "floppy", "optical"), required=True)
    parser.add_argument("--image", type=Path, required=True)
    args = parser.parse_args()
    if not sys.stdin.isatty():
        raise RuntimeError("Media writing requires an interactive terminal.")
    image = args.image.resolve(strict=True)
    if not image.is_file() or not image.stat().st_size or image.stat().st_size % 512:
        raise RuntimeError("Expected a non-empty, sector-aligned image file.")
    print("Image:", image, "("+str(image.stat().st_size)+" bytes)")
    if args.kind == "optical":
        burn(image)
    else:
        device = select(devices(args.kind))
        if device:
            flash(image, device, args.kind)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, ValueError, KeyError, subprocess.CalledProcessError,
            EOFError, KeyboardInterrupt) as exc:
        print("Media writing stopped:", exc, file=sys.stderr)
        print("If a write started, the media may be incomplete. Do not treat it as successful.", file=sys.stderr)
        sys.exit(1)
