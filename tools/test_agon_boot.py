"""Run via make test-agon on a private X server. Requires the Fab Agon Flatpak."""

from io import BytesIO
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

from PIL import Image, ImageChops


def run(*args, **kw):
    return subprocess.check_output(args, timeout=10, **kw)


def wallpaper_matches(image, pic):
    width, height = image.size
    # Sample the centre of each guest pixel; the SDL window may be scaled.
    colours = ((0, 0, 170), (255, 255, 255), (0, 0, 0), (255, 0, 0))
    for y in range(176):
        for x in range(176):
            bits = pic[14 + 44 * y + x // 4] >> (3 - x % 4)
            pen = (bits >> 4 & 1) | (bits & 1) << 1
            point = ((2*(72+x)+1)*width//640, (2*(12+y)+1)*height//400)
            if image.getpixel(point) != colours[pen]:
                return False
    return True


def calc_display_matches(image, text, font):
    # Calculator's black display is 27 byte-columns wide and right aligned.
    # Check every pixel of its interior, including the margin and blank area.
    start = (26 + 27 - (6*len(text)+3)//4 - 1)*4
    for y in range(42, 60):
        for x in range(108, 208):
            digit, bit = divmod(x-start, 6)
            ink = False
            if 0 <= digit < len(text) and 47 <= y < 55:
                row = font[16 + 8*(ord(text[digit])-32) + y-47]
                ink = bool(row & (128 >> bit))
            point = ((2*x+1)*image.width//640, (2*y+1)*image.height//400)
            if image.getpixel(point) != ((255, 0, 0) if ink else (0, 0, 0)):
                return False
    return True


def preview_matches(image, raw, mouse):
    from struct import unpack_from
    w, h = unpack_from("<HH", raw, 6)
    dw, dh = w, h
    if w > 240 or h > 144:
        dw, dh = ((240, max(1, h*240//w)) if w*144 > h*240
                  else (max(1, w*144//h), 144))
    left, top = 40+(240-dw)//2, 34+(144-dh)//2
    mode = raw[5]
    pens = {0:(0,0,170), 1:(255,255,255), 2:(0,0,0), 3:(255,0,0)}
    seen = set()
    for y in range(dh):
        sy = y*h//dh
        for x in range(dw):
            if (0 <= left+x-mouse[0] < 8 and 0 <= top+y-mouse[1] < 8):
                continue
            sx = x*w//dw
            if mode == 7:
                b = raw[14+sy*((w+1)//2)+sx//2]
                pen = b & 15 if sx & 1 else b >> 4
            elif mode == 6:
                b = raw[14+sy*((w+3)//4)+sx//4]
                pen = (b >> (6-2*(sx%4))) & 3
            else:
                b = raw[14+sy*((w+3)//4)+sx//4] >> (3-sx%4)
                pen = ((b>>4)&1) | ((b&1)<<1)
            rgb = image.getpixel(((2*(left+x)+1)*image.width//640,
                                  (2*(top+y)+1)*image.height//400))
            if pens.setdefault(pen, rgb) != rgb: return False
            seen.add(rgb)
    return len(seen) >= (8 if mode == 7 else 3)


def label_matches(image, font, x, y, text, selected=False, blue=False):
    ink, paper = ((255, 255, 255), (0, 0, 0)) if selected else ((0, 0, 0), (255, 255, 255))
    if blue:
        ink, paper = (255, 255, 255), (0, 0, 170)
    for n, char in enumerate(text):
        for dy in range(8):
            bits = font[16 + (ord(char)-32)*8 + dy]
            for dx in range(6):
                point = ((2*(x+6*n+dx)+1)*image.width//640, (2*(y+dy)+1)*image.height//400)
                if image.getpixel(point) != (ink if bits & (128 >> dx) else paper):
                    return False
    return True


def smoke(card, host):
    pic = (card / "PICS/LOGO.PIC").read_bytes()
    cpc = Path(__file__).resolve().parents[1] / "QA/CPC/CARD/GBENCH"
    font = (cpc / "DEFAULT.FNT").read_bytes()
    calc = (cpc / "CALC.APP").read_bytes()
    ist = (cpc / "DEFAULT.IST").read_bytes()
    assert (card / "GBENCH/CALC.APP").read_bytes() == calc, "not the release Calculator"
    # Place the private SD copy under the checkout: Flatpak has a private /tmp.
    with tempfile.TemporaryDirectory(dir=card.parent) as temp:
        sd = Path(temp, "card")
        shutil.copytree(card, sd)
        logo = sd / "PICS/LOGO.PIC"
        cmd = [*host, "env", f"DISPLAY={os.environ['DISPLAY']}",
               f"XAUTHORITY={os.environ['XAUTHORITY']}", "flatpak", "run",
               "--socket=x11", "--nosocket=wayland", "--env=SDL_VIDEODRIVER=x11",
               "--env=SDL_AUDIODRIVER=dummy", f"--filesystem={sd}:ro",
               "io.github.tomm.fab_agon_emulator", "--renderer", "sw", "--sdcard", str(sd)]
        process = subprocess.Popen(cmd)
        try:
            win = run("xdotool", "search", "--sync", "--onlyvisible", "--name",
                      "Fab Agon Emulator").splitlines()[0].decode()
            run("xdotool", "windowfocus", win)
            run("xdotool", "mousemove", "--window", win, "150", "120")

            def screen():
                return Image.open(BytesIO(run("import", "-window", win, "png:-"))).convert("RGB")

            def key(name):
                run("xdotool", "key", "--clearmodifiers", name)

            def move_keys(name, count):
                run("xdotool", "keydown", "Shift_L")
                try:
                    run("xdotool", "key", "--repeat", str(count), "--delay", "80", name)
                finally:
                    run("xdotool", "keyup", "Shift_L")

            def launch():
                run("xdotool", "type", "--delay", "75", "geobench")
                key("Return")

            def wait_for(matches, message, timeout=8):
                until = time.monotonic() + timeout
                while time.monotonic() < until:
                    shot = screen()
                    if matches(shot):
                        return shot
                    time.sleep(.2)
                capture = os.environ.get("AGON_CAPTURE_DIR")
                if capture:
                    Path(capture).mkdir(parents=True, exist_ok=True)
                    shot.save(Path(capture, "last-failure.png"))
                raise AssertionError(message)

            def wait_wallpaper():
                return wait_for(lambda s: wallpaper_matches(s, pic), "VDP wallpaper differs")

            def calculator(value):
                wait_for(lambda s: calc_display_matches(s, value, font),
                         f"Calculator display does not read {value}")

            def same_screen(reference):
                try:
                    wait_for(lambda s: s.tobytes() == reference.tobytes(),
                             "desktop not fully restored")
                except AssertionError:
                    actual = screen()
                    capture = os.environ.get("AGON_CAPTURE_DIR")
                    if capture:
                        Path(capture).mkdir(parents=True, exist_ok=True)
                        reference.save(Path(capture, "expected.png"))
                        actual.save(Path(capture, "actual.png"))
                    raise AssertionError(f"desktop differs at {ImageChops.difference(actual, reference).getbbox()}")

            def label(x, y, text):
                return wait_for(lambda s: label_matches(s, font, x, y, text), f"missing {text!r}")

            def title(text):
                return wait_for(lambda s: label_matches(s, font, 36, 19, text, True),
                                f"File Manager did not show {text!r}")

            def filename(x, y, text):
                return wait_for(lambda s: label_matches(s, font, x, y, text, blue=True),
                                f"File Manager entry {text!r} missing")

            def double_click():
                key("space")
                time.sleep(.15)
                key("space")

            def icon(slot, x, y, half=False):
                import struct
                start, wb, h = struct.unpack_from("<HBB", ist, 16+4*slot)
                if half and h > 16:
                    start += wb*((h-16)//2)
                    h = 16
                def matches(s):
                    for dy in range(h):
                        for dx in range(wb*4):
                            b = ist[start+dy*wb+dx//4] >> (3-dx%4)
                            pen = ((b>>4)&1) | ((b&1)<<1)
                            color = ((0,0,170), (255,255,255), (0,0,0), (255,0,0))[pen]
                            if s.getpixel(((2*(x+dx)+1)*s.width//640,
                                           (2*(y+dy)+1)*s.height//400)) != color:
                                return False
                    return True
                return wait_for(matches, f"IST icon {slot} missing at {x},{y}")

            def select(name):
                def row(image, active):
                    return next((r for r in range(10) if label_matches(
                        image, font, 44, 53+r*11, name, active)), None)
                shot = wait_for(lambda s: row(s, True) is not None or row(s, False) is not None,
                                f"directory entry {name!r} missing")
                if row(shot, True) is not None:
                    return
                # A letter cycles through matching entries on this page.
                for _ in range(10):
                    key(name[0].lower())
                    time.sleep(.15)
                    if row(screen(), True) is not None:
                        return
                raise AssertionError(f"could not select {name!r}")

            time.sleep(3)  # MOS and the VDP boot independently
            launch()
            desktop = wait_wallpaper()
            def pointer_at(s, x, y):
                # Foreground pixels from the 8x8 GEOBENCH pointer sprite.
                for dx, dy, expected in ((0,0,0), (0,1,0), (1,2,255), (2,2,0),
                                         (2,3,255), (3,3,0), (1,6,255), (1,7,0)):
                    point = ((2*(x+dx)+1)*s.width//640, (2*(y+dy)+1)*s.height//400)
                    if s.getpixel(point) != (expected,)*3: return False
                return True
            def tap_arrow(name):
                run("xdotool", "keydown", name, "sleep", ".025", "keyup", name)
            tap_arrow("Right")
            wait_for(lambda s: pointer_at(s,17,16), "Right tap was not one pixel")
            tap_arrow("Left")
            same_screen(desktop)
            run("xdotool", "keydown", "Right")
            run("xdotool", "keydown", "Down")
            try:
                wait_for(lambda s: any(pointer_at(s,x,y) for x in range(19,72)
                                       for y in range(x-3,x+4)), "held arrows did not move diagonally")
            finally:
                run("xdotool", "keyup", "Right", "Down")
            time.sleep(.12)
            stopped = screen()
            time.sleep(.2)
            assert screen().tobytes() == stopped.tobytes(), "pointer moved after release"
            move_keys("Left", 10)
            move_keys("Up", 10)
            move_keys("Right", 2)
            move_keys("Down", 2)
            same_screen(desktop)
            key("c")
            calculator("0")
            # Move the pointer from (16,16) to the 7 key; holding Space must
            # produce one ordinary GB_MSG_CLICK, even past keyboard repeat.
            move_keys("Right", 12)
            move_keys("Down", 10)
            run("xdotool", "keydown", "space")
            time.sleep(1.3)
            run("xdotool", "keyup", "space")
            calculator("7")
            move_keys("Left", 12)
            move_keys("Up", 10)
            key("c")
            calculator("0")
            run("xdotool", "type", "--delay", "100", "6x7=")
            calculator("42")
            key("Escape")
            same_screen(desktop)
            key("f")
            title("SD:")
            icon(8, 68, 31)  # first sorted directory: GBENCH
            move_keys("Right", 6)
            move_keys("Down", 3)
            double_click()
            title("SD:/GBENCH")
            icon(20, 144, 31)
            filename(124, 108, "DEFAULT.IST")
            # These draws are submitted consecutively. Check the whole set in
            # one frame, not just the last glyph to reach the display.
            def folder_frame(s):
                return (all(label_matches(s, font, x, 19, name, selected=True)
                            for x, name in ((36, "SD:/GBENCH"), (244, "View"), (276, "X")))
                        and all(label_matches(s, font, x, y, name, blue=True)
                                for x, y, name in ((124, 64, "CALC.APP"),
                                                   (200, 64, "FILEMGR.APP"),
                                                   (48, 108, "PICVIEW.APP"),
                                                   (124, 108, "DEFAULT.IST"))))
            wait_for(folder_frame, "corrupt File Manager title, controls or directory labels")
            capture = os.environ.get("AGON_CAPTURE_DIR")
            if capture:
                folder = Path(capture)
                folder.mkdir(parents=True, exist_ok=True)
                screen().save(folder / "agon-file-manager.png")
            move_keys("Right", 10)
            double_click()
            wait_for(lambda s: label_matches(s, font, 100, 27, "Calculator", True),
                     "File Manager did not launch Calculator")
            move_keys("Up", 3)  # keep the sprite off the display being compared
            calculator("0")
            run("xdotool", "type", "--delay", "100", "6x7=")
            calculator("42")
            key("Escape")
            title("SD:/GBENCH")
            move_keys("Right", 13)
            key("space")  # title-bar View control
            icon(20, 48, 49, half=True)
            move_keys("Left", 29)
            key("Escape")
            same_screen(desktop)
            manager = sd / "GBENCH/FILEMGR.APP"
            manager_bytes = manager.read_bytes()
            manager.write_bytes(manager_bytes[:-1])
            key("f")
            wait_for(lambda s: wallpaper_matches(s, pic) and
                     s.tobytes() != desktop.tobytes(), "missing File Manager error")
            manager.write_bytes(manager_bytes)
            key("f")
            title("SD:")
            move_keys("Right", 16)
            move_keys("Down", 3)
            double_click()
            title("SD:/PICS")
            filename(124, 64, "1984.PIC")
            def preview(name, mouse):
                raw = (sd / "PICS" / name).read_bytes()
                double_click()
                shot = wait_for(lambda s: label_matches(s, font, 40, 20, name, True)
                                and preview_matches(s, raw, mouse),
                                f"preview differs for {name}", timeout=30)
                if capture:
                    shot.save(Path(capture, f"preview-{name}.png"))
                key("Escape")
                title("SD:/PICS")

            preview("1984.PIC", (144,40))
            move_keys("Left", 10)
            move_keys("Down", 6)
            preview("46416.PIC", (64,88))
            move_keys("Right", 20)
            move_keys("Up", 6)
            damaged = sd / "PICS/464.PIC"
            original = damaged.read_bytes()
            damaged.write_bytes(original[:1000])
            double_click()
            filename(48, 45, "Invalid or unsupported")  # sprite covers the end
            key("Escape")
            title("SD:/PICS")
            # Odd width and nonzero padding in Mode 6: neither the padding
            # nor the previous (failed) upload may leak into the bitmap.
            damaged.write_bytes(b"GBPC" + bytes((2,6,5,0,3,0,1,26,0,6,
                                                 0x1b,0x3f,0x6c,0x7f,0xb1,0xbf)))
            preview("464.PIC", (224,40))
            damaged.write_bytes(original)
            move_keys("Left", 10)
            move_keys("Left", 13)
            move_keys("Down", 17)
            key("space")  # down arrow on the shared scrollbar
            filename(48, 64, "46416.PIC")
            filename(200, 152, "POLYMAR.PIC")  # last cell has finished painting
            key("v")
            filename(84, 54, "1984.PIC")
            move_keys("Right", 6)
            move_keys("Up", 18)
            double_click()  # ".."
            filename(84, 72, "GEOBENCH.BIN")  # pointer covers first row
            move_keys("Left", 9)
            move_keys("Up", 2)
            key("Escape")
            same_screen(desktop)
            key("b")  # compact long-name browser remains separately accessible
            select("GBENCH")
            key("Return")
            label(44, 35, "/GBENCH")
            select("CALC.APP")
            key("Return")
            calculator("0")
            run("xdotool", "type", "--delay", "100", "6x7=")
            calculator("42")
            key("Escape")
            label(44, 35, "/GBENCH")
            wait_for(lambda s: any(label_matches(s, font, 44, 53+r*11, "CALC.APP", True)
                                   for r in range(10)),
                     "Calculator selection was not restored")
            other = sd / "EXAMPLES"
            other.mkdir()
            (other / "CALC.APP").write_bytes(b"not Calculator")
            (other / "EMPTY").mkdir()
            many = other / "MANY"
            many.mkdir()
            for i in range(13):
                (many / f"ITEM{i:02}.TXT").touch()
            key("BackSpace")
            select("EXAMPLES")
            key("Return")
            select("EMPTY")
            key("Return")
            label(44, 169, "Empty directory")
            key("BackSpace")
            select("MANY")
            key("Return")
            label(44, 169, "More files below")
            for r in range(1, 11):
                key("plus")
                if r < 10:
                    wait_for(lambda s: s.getpixel((81*s.width//640,
                             (2*(52+11*r)+1)*s.height//400)) == (0, 0, 0),
                             f"+ did not select row {r+1}")
            label(44, 169, "Last page")
            key("Return")
            label(44, 169, "No application for this file")
            key("minus")
            label(44, 169, "More files below")
            key("BackSpace")
            select("CALC.APP")
            key("Return")
            label(44, 169, "CALC.APP missing or invalid")
            (other / "CALC.APP").write_bytes(calc)
            key("Return")
            calculator("0")
            key("Escape")
            label(44, 35, "/EXAMPLES")
            key("Escape")
            same_screen(desktop)
            # Erase, then redraw with the SD file missing: this requires the cached bitmap.
            logo.unlink()
            key("w")
            time.sleep(.3)
            hidden = screen()
            w, h = hidden.size
            region = hidden.crop((72*w//320, 12*h//200, 248*w//320, 188*h//200))
            assert region.getextrema() == ((0, 0), (0, 0), (170, 170)), "logo not erased"
            key("b")
            select("GBENCH")
            key("Escape")
            same_screen(hidden)
            key("c")
            calculator("0")
            key("Escape")
            same_screen(hidden)
            key("w")
            wait_wallpaper()

            # Refuse corrupt/unsupported executables without entering the last
            # app still in RAM. A valid replacement must work without rebooting.
            guest = sd / "GBENCH/CALC.APP"
            for payload in (None, calc[:-1], calc+b"!", bytes([calc[0] ^ 1])+calc[1:]):
                if payload is None:
                    guest.unlink()
                else:
                    guest.write_bytes(payload)
                key("c")
                wait_for(lambda s: wallpaper_matches(s, pic) and
                         s.tobytes() != desktop.tobytes(), "no guest load error")
                guest.write_bytes(calc)
                key("c")
                calculator("0")
                key("Escape")
                same_screen(desktop)
            key("Escape")
            time.sleep(.5)
            for fault, payload in (("missing", None), ("truncated", pic[:20])):
                if payload is not None:
                    logo.write_bytes(payload)
                launch()
                time.sleep(.5)
                if wallpaper_matches(screen(), pic):
                    raise AssertionError(f"display entered on {fault} file")
            logo.write_bytes(pic)
            launch()  # Also proves the prior invocations returned to a functioning MOS.
            wait_wallpaper()
            run("xdotool", "mousedown", "1")  # first physical input is a click
            time.sleep(.12)
            run("xdotool", "mouseup", "1")
            title("SD:")
            filename(48, 108, "GEOBENCH.BIN")
            key("Escape")
            same_screen(desktop)
            # SDL forwards these as relative PS/2 packets. Establish the
            # physical position, then use keyboard motion to reach the SD
            # target regardless of the host's mouse speed or starting point.
            run("xdotool", "mousemove_relative", "--", "24", "8")
            wait_for(lambda s: s.tobytes() != desktop.tobytes(),
                     "physical mouse motion did not move the pointer")
            move_keys("Left", 40)
            move_keys("Up", 25)
            move_keys("Right", 2)
            move_keys("Down", 2)
            same_screen(desktop)
            run("xdotool", "mousedown", "1")
            time.sleep(.12)
            run("xdotool", "mouseup", "1")
            title("SD:")
            filename(48, 108, "GEOBENCH.BIN")
            key("Escape")
            same_screen(desktop)
            key("Escape")
            print("Agon MOS: GBPC previews (modes 1/6/7) and short-read recovery, "
                  "external FILEMGR.APP, full/half IST icons, view, scrolling, child return, "
                  "keyboard pointer, held Space, PS/2 mouse, wallpaper, directories, paging, "
                  "unchanged CALC.APP (6 x 7 = 42), close, invalid-file recovery and relaunch OK")
        finally:
            if process.poll() is None:
                subprocess.run(["xdotool", "key", "Control_R+q"], check=False)
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=5)


if __name__ == "__main__":
    smoke(Path(sys.argv[1]).resolve(), sys.argv[2:])
