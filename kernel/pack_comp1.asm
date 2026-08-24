;; pack_comp1.asm - COMPANION floppy, pass 1 of 4 (#250).
;;
;; The GEOBENCH working set uses a Main floppy (build/gbkern.dsk ->
;; QA/CPC/Floppies/GEOBENCH.DSK, bootable, OS + core apps) and this Companion
;; DATA floppy (build/companion.dsk -> QA/CPC/Floppies/COMPANION.DSK, no kernel) carrying the
;; extras: Paint, Telnet, Xaos, and the screensavers. Gallery pictures now live
;; on the separate EXTRAS.DSK (#379/#384). The
;; Companion is meant for drive B (Main stays in A); the kernel app/module loader now
;; tries the boot drive (A) first, then falls back to the browse drive (B), so a
;; Companion app loads from B while shared modules (GBUI.MOD/GBNET.MOD) load from
;; A. Browser's private GBIMG.MOD and PAINT.IST stay beside their apps here.
;;
;; Like pack_apps*.asm: each of five passes is a fresh 64K rasm image whose `save ...,DSK,...`
;; APPENDS to the same .dsk; pass 1 here CREATES build/companion.dsk (build_kernel.sh
;; `rm -f`s it first). The org is irrelevant - the blobs are only assembled so the
;; save directive can write them to the disk catalogue. Keep each pass's incbin span
;; below #FFFF (org #4000 -> <= ~49KB).
;;
;; Pass 1: the three original Companion apps.
                org   #4000
pnt_img         incbin "../build/PAINT.RAW"     ; PAINT.APP (#114) - moved off Main (#250)
pnt_imgend
                save  "PAINT.APP",pnt_img,pnt_imgend-pnt_img,DSK,"build/companion.dsk"
pist_img        incbin "../build/PAINT.IST"
pist_imgend
                save  "PAINT.IST",pist_img,pist_imgend-pist_img,DSK,"build/companion.dsk"
tel_img         incbin "../build/TELNET.RAW"    ; TELNET.APP (#238) - was card-only; now floppy too
tel_imgend
                save  "TELNET.APP",tel_img,tel_imgend-tel_img,DSK,"build/companion.dsk"
xao_img         incbin "../build/XAOS.RAW"      ; XAOS.APP (#116) - moved off Main (#250)
xao_imgend
                save  "XAOS.APP",xao_img,xao_imgend-xao_img,DSK,"build/companion.dsk"
