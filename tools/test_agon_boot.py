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
            point = ((2*(72+x)+1)*width//640, (2*(32+y)+1)*height//480)
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
            point = ((2*x+1)*image.width//640, (2*y+1)*image.height//480)
            if image.getpixel(point) != ((255, 0, 0) if ink else (0, 0, 0)):
                return False
    return True


def preview_matches(image, raw, mouse, viewport=(40,34,240,144)):
    from struct import unpack_from
    w, h = unpack_from("<HH", raw, 6)
    dw, dh = w, h
    left, top, vw, vh = viewport
    if w > vw or h > vh:
        dw, dh = ((vw, max(1, h*vw//w)) if w*vh > h*vw
                  else (max(1, w*vh//h), vh))
    left, top = left+(vw-dw)//2, top+(vh-dh)//2
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
                                  (2*(top+y)+1)*image.height//480))
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
                point = ((2*(x+6*n+dx)+1)*image.width//640, (2*(y+dy)+1)*image.height//480)
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
               "--env=SDL_AUDIODRIVER=dummy", f"--filesystem={sd}:rw",
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
                # Hold through a guest video frame; MOS exposes the most
                # recent key packet, so a synthetic 6 ms tap can be missed.
                run("xdotool", "key", "--clearmodifiers", "--delay", "80", name)

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
                def difference(s):
                    diff = ImageChops.difference(s,reference)
                    # The System clock remains live while windows are open.
                    diff.paste((0,0,0),(284*s.width//320,0,314*s.width//320,8*s.height//240))
                    return diff.getbbox()
                try:
                    wait_for(lambda s: difference(s) is None,
                             "desktop not fully restored")
                except AssertionError:
                    actual = screen()
                    capture = os.environ.get("AGON_CAPTURE_DIR")
                    if capture:
                        Path(capture).mkdir(parents=True, exist_ok=True)
                        reference.save(Path(capture, "expected.png"))
                        actual.save(Path(capture, "actual.png"))
                    raise AssertionError(f"desktop differs at {difference(actual)}")

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

            def drag(horizontal, vertical):
                run("xdotool", "keydown", "space")
                time.sleep(.15)
                try:
                    for delta, positive, negative in ((horizontal, "Right", "Left"),
                                                       (vertical, "Down", "Up")):
                        if delta:
                            move_keys(positive if delta > 0 else negative, abs(delta))
                finally:
                    run("xdotool", "keyup", "space")

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
                                           (2*(y+dy)+1)*s.height//480)) != color:
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
                    point = ((2*(x+dx)+1)*s.width//640, (2*(y+dy)+1)*s.height//480)
                    if s.getpixel(point) != (expected,)*3: return False
                return True
            def tap_arrow(name):
                run("xdotool", "keydown", name, "sleep", ".025", "keyup", name)
            def accessories():
                capture=os.environ.get("AGON_CAPTURE_DIR")
                if capture: Path(capture).mkdir(parents=True,exist_ok=True)
                def menu_roundtrip():
                    # Compare at the same pointer position over both the bare
                    # desktop and a guest. Clock must not paint to its left.
                    move_keys("Up",2)
                    before=wait_for(lambda s: pointer_at(s,16,0), "pointer not on System")
                    for dismiss in ("Escape", "t", "Escape"):
                        key("space")
                        label(8,34,"Run saver")
                        key(dismiss)
                        if dismiss=="t":
                            label_clock=lambda s: label_matches(s,font,100,23,"Clock",True)
                            left=(0,8*before.height//240,96*before.width//320,142*before.height//240)
                            wait_for(lambda s: label_clock(s) and s.crop(left)==before.crop(left),
                                     "opening Clock damaged pixels to its left")
                            key("Escape")
                        same_screen(before)
                    move_keys("Down",2)
                menu_roundtrip()
                # Wake Squares from the desktop twice. F must not open Files;
                # an arrow is pointer motion, so undo its nudge for comparison.
                for wake in ("f", "Right"):
                    key("v")
                    wait_for(lambda s: not label_matches(s,font,136,0,"GEOBENCH"),
                             "desktop saver did not take over")
                    if wake == "Right":
                        tap_arrow("Right")
                        wait_for(lambda s: pointer_at(s,17,16), "arrow did not wake the pointer")
                        tap_arrow("Left")
                    else:
                        key(wake)
                    same_screen(desktop)
                # The System menu also works over a guest, and waking a saver
                # must consume the event instead of closing that guest.
                key("f")
                title("SD:")
                manager=wait_for(lambda s: any(label_matches(s,font,x,y,"GEOBENCH.BIN",blue=True)
                                 for x,y in ((200,64),(48,108))), "File Manager did not finish drawing")
                menu_roundtrip()
                move_keys("Up",2)
                key("space")
                label(8,34,"Run saver")
                move_keys("Down",5)
                key("space")
                wait_for(lambda s: not label_matches(s,font,136,0,"GEOBENCH"),
                         "System menu did not start Squares over Files")
                time.sleep(.4)
                key("Escape")  # must not close the underlying File Manager
                title("SD:")
                move_keys("Up",3)
                same_screen(manager)
                move_keys("Up",2)
                key("space")
                label(8,10,"Settings")
                move_keys("Down",2)
                key("space")
                settings = wait_for(lambda s: label_matches(s,font,64,40,"Settings",True) and
                                    label_matches(s,font,196,128,"Cancel"),
                                    "Settings did not open")
                if capture: settings.save(Path(capture,"agon-settings.png"))
                key("p")
                wait_for(lambda s: not label_matches(s,font,64,40,"Settings",True) and
                         not label_matches(s,font,136,0,"GEOBENCH"), "Squares did not start")
                time.sleep(.25)
                squares=screen()
                wait_for(lambda s: s.tobytes()!=squares.tobytes(), "Squares did not animate")
                key("z")
                same_screen(settings)
                move_keys("Right",18)
                move_keys("Down",6)   # x160, y64: Squares -> Blank
                key("space")
                move_keys("Down",2)   # y80: 2 min -> 5 min -> Off -> 15 seconds
                for _ in range(3):
                    key("space")
                    time.sleep(.15)
                move_keys("Down",2)   # y96: seconds hand on
                key("space")
                move_keys("Left",18)
                label(156,57,"Blank")
                label(156,75,"15 seconds")
                label(156,93,"On")
                # A failed write (e.g. write-protected SD) must not report
                # success or throw away the pending edits.
                blocked = sd/"GBENCH/AGON.CFG"
                blocked.mkdir()
                key("Return")
                label(68,108,"Cannot write AGON.CFG")
                blocked.rmdir()
                key("Return")
                wait_for(lambda s: not label_matches(s,font,64,40,"Settings",True) and
                         label_matches(s,font,36,19,"SD:",True), "Settings did not save and close")
                config=next(p for p in (sd/"GBENCH").iterdir() if p.name.upper()=="AGON.CFG")
                assert config.read_bytes()==b"GBA1\x01\x01\x01\xa4", "wrong MOS preference record"
                wait_for(lambda s: s.getextrema()==((0,0),)*3,
                         "idle timeout did not start Blank over File Manager", timeout=21)
                key("Escape")
                title("SD:")
                key("Escape")
                key("Escape")
                time.sleep(.5)
                launch()
                wait_wallpaper()
                key("s")
                label(156,57,"Blank")
                label(156,75,"15 seconds")
                label(156,93,"On")
                key("Escape")
                key("t")
                clock=wait_for(lambda s: label_matches(s,font,100,23,"Clock",True) and
                               label_matches(s,font,140,126,":",blue=True), "no analog clock/RTC")
                if capture: clock.save(Path(capture,"agon-clock.png"))
                def digital(s):
                    return s.crop((128*s.width//320,126*s.height//240,
                                   176*s.width//320,134*s.height//240)).tobytes()
                wait_for(lambda s: digital(s)!=digital(clock), "clock did not tick while idle")
                key("Escape")
                key("Escape")

            if os.environ.get("AGON_SYSTEM_ONLY"):
                accessories()
                print("Agon System menu, Settings, RTC and screensavers OK")
                return
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
            # Move from the title, then shrink from the lower-right grip.
            # The SD artwork and uncovered wallpaper must survive both.
            move_keys("Right", 6)
            card_icon = icon(13, 8, 16)
            if os.environ.get("AGON_CAPTURE_DIR"):
                folder = Path(os.environ["AGON_CAPTURE_DIR"])
                folder.mkdir(parents=True, exist_ok=True)
                card_icon.save(folder / "agon-desktop.png")
            key("f")
            title("SD:")
            drag(2,2)
            wait_for(lambda s: label_matches(s,font,52,35,"SD:",True),
                     "File Manager title did not move")
            filename(64,80,"GBENCH")
            move_keys("Right",27)
            move_keys("Down",20)
            for _ in range(3):
                tap_arrow("Down")
            drag(-6,-3)
            wait_for(lambda s: label_matches(s,font,244,35,"X",True),
                     "File Manager did not resize")
            def exposed(s):
                area = (260*s.width//320, 60*s.height//240,
                        304*s.width//320, 200*s.height//240)
                return s.crop(area).tobytes() == desktop.crop(area).tobytes()
            wait_for(exposed, "resizing left window pixels on the desktop")
            key("Escape")
            move_keys("Left",40)
            move_keys("Up",30)
            move_keys("Right",2)
            move_keys("Down",2)
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
                if name == "1984.PIC":
                    # Moving must reuse the bitmap even without the source file.
                    path = sd / "PICS" / name
                    path.unlink()
                    move_keys("Up",2)
                    drag(2,2)
                    wait_for(lambda s: preview_matches(s,raw,(160,40),(56,50,240,144)),
                             "moving the preview lost its bitmap")
                    path.write_bytes(raw)
                    move_keys("Right",17)
                    move_keys("Down",19)
                    drag(-4,-2)
                    wait_for(lambda s: preview_matches(s,raw,(264,176),(56,50,208,128)),
                             "resizing did not refit the picture", timeout=30)
                    move_keys("Left",15)
                    move_keys("Up",17)
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
                             (2*(52+11*r)+1)*s.height//480)) == (0, 0, 0),
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
            region = hidden.crop((72*w//320, 32*h//240, 248*w//320, 208*h//240))
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
            move_keys("Right", 6)  # title bar, away from the View/close gadgets
            run("xdotool", "mousedown", "1", "sleep", ".15", "mousemove_relative",
                "--", "28", "16", "sleep", ".15", "mouseup", "1")
            wait_for(lambda s: any((x,y) != (36,19) and label_matches(
                        s,font,x,y,"SD:",True) for y in range(11,67)
                        for x in range(4,65,4)), "PS/2 mouse did not drag File Manager")
            key("Escape")
            move_keys("Left",40)
            move_keys("Up",30)
            move_keys("Right",2)
            move_keys("Down",2)
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
            accessories()
            print("Agon MOS: GBPC previews (modes 1/6/7) and short-read recovery, "
                  "external FILEMGR.APP, full/half IST icons, view, scrolling, child return, "
                  "SD card artwork, window drag/resize and bitmap restore, keyboard pointer, "
                  "held Space, PS/2 mouse, wallpaper, directories, paging, "
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
