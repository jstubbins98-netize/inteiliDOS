#!/usr/bin/env python3
"""Create a separate, user-supplied DOS media CD; never an inteiliDOS ISO."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
from prepare_msdos622 import prepare


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images", type=Path, help="Folder containing your four DOS disks")
    parser.add_argument("output", type=Path, help="Separate DOS media .iso output")
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        raise SystemExit(f"Refusing to overwrite {output}")
    tool = shutil.which("xorriso") or shutil.which("mkisofs") or shutil.which("genisoimage")
    if not tool:
        raise SystemExit("Creating a DOS CD requires xorriso, mkisofs, or genisoimage.")
    with tempfile.TemporaryDirectory(prefix="msdos-media-cd-") as folder:
        tmp = Path(folder)
        prepare(args.images.resolve(), tmp / "disc")
        iso = tmp / "media.iso"
        command = [tool]
        if Path(tool).name == "xorriso":
            command += ["-as", "mkisofs"]
        subprocess.run(command + ["-quiet", "-iso-level", "1", "-V", "MSDOS_MEDIA",
                                  "-o", str(iso), str(tmp / "disc")], check=True)
        output.parent.mkdir(parents=True, exist_ok=True)
        with iso.open("rb") as source, output.open("xb") as target:
            shutil.copyfileobj(source, target)
    print(f"Created separate DOS media CD: {output}")


if __name__ == "__main__":
    main()
