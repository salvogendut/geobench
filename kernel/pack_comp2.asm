;; pack_comp2.asm - COMPANION floppy, pass 2 of 4 (#250). See pack_comp1.asm header.
;;
;; Pass 2: the first half of the screensaver set. SQUARES stays on Main as the
;; default and is not duplicated here; that block now carries PAINT.IST in pass 1.
;; The .SAV names mirror tools/stage_dist.sh (build/<NAME>.RAW -> <NAME>.SAV).
                org   #4000
dec_img         incbin "../build/DECO.RAW"      ; art-deco panels
dec_imgend
                save  "DECO.SAV",dec_img,dec_imgend-dec_img,DSK,"build/companion.dsk"
xmx_img         incbin "../build/XMATRIX.RAW"   ; Matrix digital rain
xmx_imgend
                save  "XMATRIX.SAV",xmx_img,xmx_imgend-xmx_img,DSK,"build/companion.dsk"
xmc_img         incbin "../build/XMATRIXCFG.RAW" ; same-stem Configure companion
xmc_imgend
                save  "XMATRIX.MOD",xmc_img,xmc_imgend-xmc_img,DSK,"build/companion.dsk"
stf_img         incbin "../build/STARFLD.RAW"   ; 3D star-field
stf_imgend
                save  "STARFLD.SAV",stf_img,stf_imgend-stf_img,DSK,"build/companion.dsk"
stc_img         incbin "../build/STARFLDCFG.RAW" ; same-stem Configure companion
stc_imgend
                save  "STARFLD.MOD",stc_img,stc_imgend-stc_img,DSK,"build/companion.dsk"
frc_img         incbin "../build/FRACTALI.RAW"  ; fractal (Sierpinski/Koch/Dragon/Fern)
frc_imgend
                save  "FRACTALI.SAV",frc_img,frc_imgend-frc_img,DSK,"build/companion.dsk"
