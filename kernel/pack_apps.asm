;; pack_apps.asm - second-pass .dsk packaging for GEOBENCH (#114).
;;
;; The main image (gbkern.asm) packs the kernel + every app/asset into one 64K rasm
;; address space and `save`s them into build/gbkern.dsk. That space filled up. RASM's
;; DSK save APPENDS to an existing .dsk across separate invocations, so the apps that
;; overflow the main image get packaged here, in their own fresh 64K pass, into the
;; SAME .dsk. build_kernel.sh runs this right after gbkern.asm (no rm in between).
;;
;; These blobs are #4000 app images (build_capp output); the org is irrelevant - they
;; are only assembled so the save directive can write them to the disk catalogue.
                org   #4000
pnt_img         incbin "../build/PAINT.RAW"     ; packaged on the disk as PAINT.APP (#114)
pnt_imgend
                save  "PAINT.APP",pnt_img,pnt_imgend-pnt_img,DSK,"build/gbkern.dsk"
xao_img         incbin "../build/XAOS.RAW"      ; packaged on the disk as XAOS.APP (#116)
xao_imgend
                save  "XAOS.APP",xao_img,xao_imgend-xao_img,DSK,"build/gbkern.dsk"
ied_img         incbin "../build/ICONED.RAW"    ; packaged on the disk as ICONED.APP (moved
ied_imgend                                      ; to pass 2 - the main image filled up, #142)
                save  "ICONED.APP",ied_img,ied_imgend-ied_img,DSK,"build/gbkern.dsk"
pen_img         incbin "../assets/pictures/PENGUIN.PIC" ; sample 200x200 picture -> PENGUIN.PIC
pen_imgend
                save  "PENGUIN.PIC",pen_img,pen_imgend-pen_img,DSK,"build/gbkern.dsk"
ui_img          incbin "../build/GBUI.RAW"      ; paged dialog module -> GBUI.BIN (#142 step 1b)
ui_imgend
                save  "GBUI.BIN",ui_img,ui_imgend-ui_img,DSK,"build/gbkern.dsk"
