#!/usr/bin/env python3
"""Install GEOBENCH .PIC thumbnail and viewer support for this GNOME user."""

import base64
import os
import shutil
import subprocess
import sys
import zlib
from pathlib import Path


MIME_XML = """<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <mime-type type="image/x-geobench-pic">
    <comment>GEOBENCH picture</comment>
    <icon name="image-x-generic"/>
    <glob pattern="*.PIC"/>
    <glob pattern="*.pic"/>
    <magic priority="80">
      <match type="string" value="GBPC" offset="0"/>
    </magic>
  </mime-type>
</mime-info>
"""


def write_file(path, content, mode=0o644):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    path.chmod(mode)


def main():
    tools_dir = Path(__file__).parent
    source_path = tools_dir / "gbpic_thumbnailer.py"
    viewer_source = tools_dir / "gbpic_viewer.py"
    source = source_path.read_bytes()
    payload = base64.b64encode(zlib.compress(source, 9)).decode("ascii")
    inline = (
        "import base64,zlib;"
        f"exec(zlib.decompress(base64.b64decode('{payload}')))"
    )
    thumbnailer = (
        "[Thumbnailer Entry]\n"
        "TryExec=/usr/bin/python3\n"
        f'Exec=/usr/bin/python3 -c "{inline}" %i %o %s\n'
        "MimeType=image/x-geobench-pic;\n"
    )

    data_home = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share"))
    config_home = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
    cache_home = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache"))
    config_home.mkdir(parents=True, exist_ok=True)
    mime_root = data_home / "mime"
    installed_bin = data_home / "geobench/bin"
    installed_decoder = installed_bin / "gbpic_thumbnailer.py"
    installed_viewer = installed_bin / "gbpic_viewer.py"
    write_file(mime_root / "packages/geobench-pic.xml", MIME_XML)
    write_file(data_home / "thumbnailers/geobench-pic.thumbnailer", thumbnailer)
    write_file(installed_decoder, source.decode("utf-8"), 0o755)
    write_file(installed_viewer, viewer_source.read_text(encoding="utf-8"), 0o755)

    desktop_file = data_home / "applications/org.geobench.PicViewer.desktop"
    desktop_entry = (
        "[Desktop Entry]\n"
        "Name=Image Viewer (GEOBENCH PIC)\n"
        "Comment=View GEOBENCH pictures\n"
        f"Exec=/usr/bin/python3 {installed_viewer} %f\n"
        "TryExec=/usr/bin/python3\n"
        "Icon=org.gnome.Loupe\n"
        "Terminal=false\n"
        "Type=Application\n"
        "MimeType=image/x-geobench-pic;\n"
        "NoDisplay=true\n"
        # The wrapper exits after handing the PNG to Loupe and does not own a
        # GTK startup window, so advertising startup notification makes GNOME
        # wait for a completion signal that will never arrive.
        "StartupNotify=false\n"
    )
    write_file(desktop_file, desktop_entry)

    updater = shutil.which("update-mime-database")
    if updater:
        subprocess.run((updater, str(mime_root)), check=True)
    desktop_updater = shutil.which("update-desktop-database")
    if desktop_updater:
        subprocess.run((desktop_updater, str(data_home / "applications")), check=True)
    xdg_mime = shutil.which("xdg-mime")
    if xdg_mime:
        subprocess.run((xdg_mime, "default", desktop_file.name,
                        "image/x-geobench-pic"), check=True)

    failure_dir = cache_home / "thumbnails/fail/gnome-thumbnail-factory"
    removed = 0
    if failure_dir.is_dir():
        for cached_failure in failure_dir.glob("*.png"):
            cached_failure.unlink()
            removed += 1

    print(f"Installed {data_home / 'thumbnailers/geobench-pic.thumbnailer'}")
    print(f"Installed {desktop_file}")
    print(f"Refreshed {mime_root}")
    print(f"Removed {removed} cached thumbnail failures")
    return 0


if __name__ == "__main__":
    sys.exit(main())
