#!/usr/bin/env python3
"""picconv - convert an image (PNG, JPG, ... any Pillow format) into a GEOBENCH .PIC
(Mode-1, 4 colours).

A tkinter GUI (like tools/iconedit.py) AND a CLI. It maps each pixel to the nearest
of the four GEOBENCH desktop inks (blue / white / black / red), optionally with
dithering for photos, scales to a chosen size (width snapped to a multiple of 4 -
Mode 1 packs 4 px per byte), and writes the .PIC v2 format that PAINT saves and the
Viewer shows:

    "GBPC" | ver=2 | mode=1 | width_px(2 LE) | height_px(2 LE) | inks[4] | bitmap

    pen 0  blue   #000080   CPC ink 1
    pen 1  white  #FFFFFF   CPC ink 26
    pen 2  black  #000000   CPC ink 0
    pen 3  red    #FF0000   CPC ink 6

Usage:
    tools/picconv.py                         # launch the GUI
    tools/picconv.py photo.jpg out.PIC       # convert (CLI), Floyd-Steinberg
    tools/picconv.py in.png out.PIC -d none -w 160
"""
import sys
import os
import struct
import tempfile
from PIL import Image

# pen -> display RGB and the CPC hardware ink stored in the .PIC palette.
PAL_RGB = [(0x00, 0x00, 0x80), (0xFF, 0xFF, 0xFF), (0x00, 0x00, 0x00), (0xFF, 0x00, 0x00)]
INKS    = [1, 26, 0, 6]
BIT0_FOR_PIXEL = (7, 6, 5, 4)        # Mode-1: pen bit0 of the 4 pixels in a byte
BIT1_FOR_PIXEL = (3, 2, 1, 0)        # ... pen bit1
DITHERS = ("floyd", "atkinson", "ordered", "none")

_BAYER4 = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def nearest(r, g, b):
    best, best_d = 0, None
    for pen, (pr, pg, pb) in enumerate(PAL_RGB):
        d = (pr - r) ** 2 + (pg - g) ** 2 + (pb - b) ** 2
        if best_d is None or d < best_d:
            best, best_d = pen, d
    return best


def _clamp(v):
    return 0 if v < 0 else 255 if v > 255 else v


def quantize(img, dither):
    """RGB PIL image -> 2D list of pen indices (height x width)."""
    w, h = img.size
    px = img.load()
    if dither == "none":
        return [[nearest(*px[x, y]) for x in range(w)] for y in range(h)]
    if dither == "ordered":
        out = [[0] * w for _ in range(h)]
        for y in range(h):
            for x in range(w):
                o = (_BAYER4[y & 3][x & 3] / 15.0 - 0.5) * 64
                r, g, b = px[x, y]
                out[y][x] = nearest(_clamp(r + o), _clamp(g + o), _clamp(b + o))
        return out
    # error-diffusion (Floyd-Steinberg or Atkinson)
    r = [[float(px[x, y][0]) for x in range(w)] for y in range(h)]
    g = [[float(px[x, y][1]) for x in range(w)] for y in range(h)]
    b = [[float(px[x, y][2]) for x in range(w)] for y in range(h)]
    out = [[0] * w for _ in range(h)]
    if dither == "atkinson":
        taps, denom = [(1, 0), (2, 0), (-1, 1), (0, 1), (1, 1), (0, 2)], 8
    else:
        taps, denom = [(1, 0, 7), (-1, 1, 3), (0, 1, 5), (1, 1, 1)], 16
    for y in range(h):
        for x in range(w):
            rv, gv, bv = _clamp(r[y][x]), _clamp(g[y][x]), _clamp(b[y][x])
            pen = nearest(rv, gv, bv)
            out[y][x] = pen
            er, eg, eb = rv - PAL_RGB[pen][0], gv - PAL_RGB[pen][1], bv - PAL_RGB[pen][2]
            for tap in taps:
                dx, dy = tap[0], tap[1]
                wgt = (tap[2] if len(tap) == 3 else 1) / denom
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h:
                    r[ny][nx] += er * wgt
                    g[ny][nx] += eg * wgt
                    b[ny][nx] += eb * wgt
    return out


def pack(pens):
    """2D pens -> Mode-1 bytes (row-major, 4 px per byte)."""
    h = len(pens)
    w = len(pens[0]) if h else 0
    wb = w // 4
    data = bytearray()
    for y in range(h):
        for bx in range(wb):
            byte = 0
            for i in range(4):
                p = pens[y][bx * 4 + i]
                if p & 1:
                    byte |= 1 << BIT0_FOR_PIXEL[i]
                if p & 2:
                    byte |= 1 << BIT1_FOR_PIXEL[i]
            data.append(byte)
    return bytes(data)


def prepare(img, width):
    """RGB image scaled so its width == `width` (snapped x4), aspect kept."""
    width = max(4, (width // 4) * 4)
    sw, sh = img.size
    height = max(1, round(width * sh / sw))
    return img.resize((width, height), Image.LANCZOS)


def save_pic(path, pens, w, h):
    hdr = b"GBPC" + bytes([2, 1]) + struct.pack("<HH", w, h) + bytes(INKS)
    with open(path, "wb") as f:
        f.write(hdr)
        f.write(pack(pens))


def convert_file(in_png, out_pic, dither, width):
    img = Image.open(in_png).convert("RGB")
    img = prepare(img, width or img.size[0])
    pens = quantize(img, dither)
    w, h = img.size
    save_pic(out_pic, pens, w, h)
    return w, h


# --------------------------------------------------------------------------- GUI
def run_gui(initial=None):
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk

    class PicConv(tk.Tk):
        def __init__(self):
            super().__init__()
            self.title("picconv - PNG to .PIC")
            self.src = None             # source RGB image
            self.pens = None            # last conversion
            self.preview_img = None     # keep a ref so Tk doesn't GC it
            self._tmp = None

            bar = ttk.Frame(self, padding=6)
            bar.pack(fill="x")
            ttk.Button(bar, text="Open PNG…", command=self.open_png).pack(side="left")
            ttk.Button(bar, text="Save .PIC…", command=self.save_pic).pack(side="left", padx=(4, 12))
            ttk.Label(bar, text="Dither:").pack(side="left")
            self.dither = tk.StringVar(value="floyd")
            cb = ttk.Combobox(bar, textvariable=self.dither, values=DITHERS, width=9, state="readonly")
            cb.pack(side="left", padx=(2, 12))
            cb.bind("<<ComboboxSelected>>", lambda e: self.reconvert())
            ttk.Label(bar, text="Width:").pack(side="left")
            self.width = tk.StringVar(value="160")
            we = ttk.Entry(bar, textvariable=self.width, width=5)
            we.pack(side="left", padx=2)
            we.bind("<Return>", lambda e: self.reconvert())
            ttk.Button(bar, text="Convert", command=self.reconvert).pack(side="left", padx=(4, 0))

            self.canvas = tk.Canvas(self, width=480, height=360, bg="#202028", highlightthickness=0)
            self.canvas.pack(padx=6, pady=(0, 6))
            self.status = ttk.Label(self, text="Open a PNG to begin.", anchor="w", padding=(6, 2))
            self.status.pack(fill="x")
            if initial:
                self.load(initial)

        def open_png(self):
            p = filedialog.askopenfilename(filetypes=[
                ("Images", "*.png *.jpg *.jpeg *.gif *.bmp"), ("All", "*")])
            if p:
                self.load(p)

        def load(self, path):
            try:
                self.src = Image.open(path).convert("RGB")
            except Exception as e:
                messagebox.showerror("picconv", f"Could not open:\n{e}")
                return
            self.title(f"picconv - {os.path.basename(path)}")
            if self.width.get().strip() in ("", "160"):
                self.width.set(str(min(320, self.src.size[0])))
            self.reconvert()

        def reconvert(self):
            if self.src is None:
                return
            try:
                w = int(self.width.get())
            except ValueError:
                w = self.src.size[0]
            img = prepare(self.src, w)
            self.pens = quantize(img, self.dither.get())
            self.pw, self.ph = img.size
            self.show()
            self.status.config(text=f"{self.pw}x{self.ph} px  ->  "
                                     f"{self.pw // 4 * self.ph + 14} byte .PIC   ({self.dither.get()})")

        def show(self):
            prev = Image.new("RGB", (self.pw, self.ph))
            prev.putdata([PAL_RGB[self.pens[y][x]] for y in range(self.ph) for x in range(self.pw)])
            scale = max(1, min(480 // self.pw, 360 // self.ph)) if self.pw and self.ph else 1
            prev = prev.resize((self.pw * scale, self.ph * scale), Image.NEAREST)
            if self._tmp is None:
                self._tmp = tempfile.NamedTemporaryFile(suffix=".png", delete=False).name
            prev.save(self._tmp)
            self.preview_img = tk.PhotoImage(file=self._tmp)        # Tk 8.6+ reads PNG
            self.canvas.delete("all")
            self.canvas.create_image(240, 180, image=self.preview_img)

        def save_pic(self):
            if self.pens is None:
                return
            p = filedialog.asksaveasfilename(defaultextension=".PIC",
                                             filetypes=[("GEOBENCH picture", "*.PIC"), ("All", "*")])
            if not p:
                return
            save_pic(p, self.pens, self.pw, self.ph)
            self.status.config(text=f"Saved {os.path.basename(p)}  ({self.pw}x{self.ph})")

        def destroy(self):
            if self._tmp and os.path.exists(self._tmp):
                try:
                    os.unlink(self._tmp)
                except OSError:
                    pass
            super().destroy()

    PicConv().mainloop()


def main():
    args = sys.argv[1:]
    if not args:
        run_gui()
        return
    # CLI
    import argparse
    p = argparse.ArgumentParser(description="Convert an image (PNG/JPG/...) to a GEOBENCH .PIC.")
    p.add_argument("in_img", help="source image (any format Pillow reads: PNG, JPG, GIF, BMP...)")
    p.add_argument("out_pic", nargs="?", help="defaults to in.<ext> -> in.PIC")
    p.add_argument("-d", "--dither", choices=DITHERS, default="floyd")
    p.add_argument("-w", "--width", type=int, default=0, help="target width (snapped x4); 0 = source width")
    p.add_argument("--gui", action="store_true", help="open the GUI on this file")
    a = p.parse_args(args)
    if a.gui:
        run_gui(a.in_img)
        return
    out = a.out_pic or os.path.splitext(a.in_img)[0] + ".PIC"
    w, h = convert_file(a.in_img, out, a.dither, a.width)
    print(f"{a.in_img}: {w}x{h} -> {out}  ({a.dither}, {w // 4 * h + 14} bytes)")


if __name__ == "__main__":
    main()
