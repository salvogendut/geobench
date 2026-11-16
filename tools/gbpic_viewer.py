#!/usr/bin/env python3
"""Open a GEOBENCH picture with the desktop's normal PNG image viewer."""

import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

from gbpic_thumbnailer import load_pic, local_path


def cached_png(source):
    source_path = Path(local_path(source)).resolve()
    stat = source_path.stat()
    identity = (f"{source_path}\0{stat.st_mtime_ns}\0{stat.st_size}").encode()
    name = hashlib.sha256(identity).hexdigest() + ".png"
    cache_root = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache"))
    output_dir = cache_root / "geobench-pic-viewer"
    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / name
    if not output.exists():
        temporary = output.with_suffix(".tmp")
        load_pic(source_path).save(temporary, "PNG")
        temporary.replace(output)
    return output


def report_error(message):
    print(f"gbpic-viewer: {message}", file=sys.stderr)
    notifier = shutil.which("notify-send")
    if notifier:
        subprocess.run((notifier, "Cannot open GEOBENCH picture", str(message)),
                       check=False)


def main():
    if len(sys.argv) != 2:
        print("usage: gbpic_viewer.py INPUT.PIC", file=sys.stderr)
        return 2
    try:
        output = cached_png(sys.argv[1])
        subprocess.run(("gio", "open", output.as_uri()), check=True)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        report_error(error)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
