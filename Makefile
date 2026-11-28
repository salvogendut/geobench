.PHONY: all cpc cpc-preemptive cpc-cooperative msx msx-preemptive msx-cooperative msx-preemptive-diagnostic msx-floppies pcw pcw-preemptive pcw-cooperative pcw-preemptive-diagnostic app formref sndtest taskdemo titlebar-editor check test
.NOTPARALLEL:

all: cpc msx pcw

# Opt-in Agon MOS bring-up; not a release target yet.
AGON_CARD := QA/AGON/CARD
AGON_RUN_CARD := build/agon-sd
RASM ?= rasm
.PHONY: agon run-agon test-agon
AGON_CALC := QA/CPC/CARD/GBENCH/CALC.APP
AGON_FM := $(AGON_CARD)/GBENCH/FILEMGR.APP
AGON_IST := $(AGON_CARD)/GBENCH/DEFAULT.IST
AGON_PREVIEW := $(AGON_CARD)/GBENCH/PICVIEW.APP
AGON_NOTE := $(AGON_CARD)/GBENCH/NOTEPAD.APP
AGON_XAOS := $(AGON_CARD)/GBENCH/XAOS.APP
AGON_VIEWER := $(AGON_CARD)/GBENCH/VIEWER.APP
AGON_CAT := $(AGON_CARD)/GBENCH/AGON.CAT
AGON_PICS := $(patsubst assets/pictures/%,$(AGON_CARD)/PICS/%,$(wildcard assets/pictures/*.PIC))
agon: $(AGON_CARD)/GEOBENCH.BIN $(AGON_PICS) $(AGON_CAT) $(AGON_IST) $(AGON_CARD)/GBENCH/WELCOME.TXT

$(AGON_CARD)/GBENCH/WELCOME.TXT: assets/WELCOME.TXT
	mkdir -p $(@D)
	cp $< $@

AGON_GUESTS := 0:$(AGON_CARD)/GBENCH/CALC.APP 1:$(AGON_FM) 2:$(AGON_PREVIEW) 3:$(AGON_NOTE) 5:$(AGON_XAOS) 6:$(AGON_VIEWER)
$(AGON_FM) $(AGON_NOTE): lib/gb/gbagon.h
$(AGON_CAT): tools/agon_catalog.py $(AGON_CARD)/GBENCH/CALC.APP $(AGON_FM) $(AGON_PREVIEW) $(AGON_NOTE) $(AGON_XAOS) $(AGON_VIEWER)
	python3 $< $@ $(AGON_GUESTS)

$(AGON_NOTE): apps/notepad/main.c apps/notepad/icon.asm Makefile tools/build_capp.sh tools/build_cache.sh lib/gb/gb.h lib/gb/gbdoc.c lib/gb/gbui_stub.c lib/gb/gblib.s lib/gb/gbwin.c
	APPDEFS=-DGB_AGON APP_ICON=apps/notepad/icon.asm DOC=1 APP_CFLAGS=--opt-code-size DATA_LOC=0x6c00 bash tools/build_capp.sh apps/notepad build/agon/NOTEPAD.RAW
	cp build/agon/NOTEPAD.RAW $@

$(AGON_CARD)/GEOBENCH.BIN: kernel/agon/boot.asm kernel/agon/video.asm kernel/agon/input.asm kernel/agon/input.inc kernel/lowram.inc lib/gbapp.inc build/agon/runtime.bin build/agon/files.bin build/agon/window.bin build/agon/gesture.bin build/agon/system.bin build/agon/worker.bin build/agon/ist.inc
	mkdir -p $(@D)
	$(RASM) $< -ob $@

# Keep the trial card separate so the tested normal card remains usable.
AGON_VGA_SD := build/agon-vga-sd
AGON_VGA_BIN := QA/AGON/VGA/GBVGA.BIN
.PHONY: agon-vga run-agon-vga test-agon-vga test-agon-resolution
agon-vga: agon $(AGON_VGA_BIN)
	mkdir -p $(AGON_VGA_SD)
	cp -R $(AGON_CARD)/. $(AGON_VGA_SD)/
	cp $(AGON_VGA_BIN) $(AGON_VGA_SD)/

$(AGON_VGA_BIN): kernel/agon/boot.asm $(AGON_CARD)/GEOBENCH.BIN
	mkdir -p $(@D)
	$(RASM) $< -DAGON_VGA=1 -ob $@

run-agon-vga: agon-vga
	flatpak run --filesystem="$(abspath $(AGON_VGA_SD)):rw" io.github.tomm.fab_agon_emulator --sdcard "$(abspath $(AGON_VGA_SD))"

test-agon-vga: agon-vga
	AGON_VGA_ONLY=1 xvfb-run -a -s "-screen 0 1024x768x24" python3 tools/test_agon_boot.py $(AGON_VGA_SD) $(AGON_TEST_HOST)

test-agon-resolution: agon
	AGON_RESOLUTION_ONLY=1 xvfb-run -a -s "-screen 0 1024x768x24" python3 tools/test_agon_boot.py $(AGON_CARD) $(AGON_TEST_HOST)

# Same analog face and Squares animation as the other targets; MOS input,
# persistence and VDP line drawing are Agon-specific.
build/agon/system.bin: kernel/agon/system.c kernel/agon/guest.c kernel/agon/document.c kernel/agon/storage.c kernel/agon/storage.h kernel/agon/fileops.c kernel/agon/workspace.c kernel/agon/saver_screen.c kernel/agon/system_entry.s apps/clock/main.c apps/saver/main.c lib/gb/gbwin.c lib/gb/gblib.s lib/gb/gb.h lib/gb/gbagon.h
	mkdir -p $(@D)
	sdasz80 -o $(@D)/system_entry.rel kernel/agon/system_entry.s
	sdasz80 -o $(@D)/syslib.rel lib/gb/gblib.s
	sdcc -mz80 --opt-code-size -DGB_AGON -Ilib/gb -c kernel/agon/system.c -o $(@D)/sys.rel
	sdcc -mz80 --opt-code-size -c kernel/agon/saver_screen.c -o $(@D)/saver_screen.rel
	sdcc -mz80 --opt-code-size -DGB_AGON -Ilib/gb -c kernel/agon/workspace.c -o $(@D)/workspace.rel
	sdcc -mz80 --opt-code-size -DGB_AGON -Ilib/gb -c kernel/agon/document.c -o $(@D)/document.rel
	sdcc -mz80 --opt-code-size -Ilib/gb -c kernel/agon/storage.c -o $(@D)/storage.rel
	sdcc -mz80 --opt-code-size -DGB_AGON -Ilib/gb -c kernel/agon/fileops.c -o $(@D)/fileops.rel
	sdcc -mz80 --opt-code-size -c kernel/agon/guest.c -o $(@D)/guest.rel
	sdcc -mz80 --opt-code-size -DGB_AGON -Ilib/gb -c apps/clock/main.c -o $(@D)/clock.rel
	sdcc -mz80 --opt-code-size -DGB_AGON -Ilib/gb -c apps/saver/main.c -o $(@D)/squares.rel
	sdcc -mz80 --opt-code-size -DGB_AGON -Ilib/gb -c lib/gb/gbwin.c -o $(@D)/syswin.rel
	sdcc -mz80 --no-std-crt0 --code-loc 0xa000 --data-loc 0xe550 $(addprefix $(@D)/,system_entry.rel sys.rel saver_screen.rel workspace.rel document.rel storage.rel fileops.rel guest.rel clock.rel squares.rel syswin.rel syslib.rel) -o $(@D)/system.ihx
	python3 -c 'import re; s=open("$(@D)/system.map").read(); a,n=(int(x,16) for x in re.search(r"^_DATA\s+(\w+)\s+(\w+)",s,re.M).groups()); assert a+n<=0xeb00, "Agon system data overlaps Files"'
	makebin -s 65536 -p $(@D)/system.ihx $(@D)/system.flat
	tail -c +40961 $(@D)/system.flat > $@

build/agon/window.bin build/agon/gesture.bin &: tools/agon_window_sections.py kernel/agon/window.c kernel/agon/gesture.c kernel/agon/geometry.h kernel/agon/window_entry.s lib/gb/gb.h lib/gb/agon_display.h
	mkdir -p $(@D)
	sdasz80 -o $(@D)/window_entry.rel kernel/agon/window_entry.s
	sdcc -mz80 --opt-code-size -DGB_AGON -Ilib/gb -c kernel/agon/window.c -o $(@D)/window.rel
	sdcc -mz80 --opt-code-size --codeseg GESTURE -DGB_AGON -Ilib/gb -c kernel/agon/gesture.c -o $(@D)/gesture.rel
	sdcc -mz80 --no-std-crt0 --code-loc 0x0900 --data-loc 0x1e00 -Wl-b_GESTURE=0xf240 $(@D)/window_entry.rel $(@D)/window.rel $(@D)/gesture.rel -o $(@D)/window.ihx
	makebin -s 65536 -p $(@D)/window.ihx $(@D)/window.flat
	python3 tools/agon_window_sections.py $(@D)

build/agon/runtime.bin: kernel/agon/runtime.asm kernel/agon/draw.asm kernel/agon/input.inc lib/gbapp.inc kernel/lowram.inc QA/CPC/CARD/GBENCH/DEFAULT.FNT
	mkdir -p $(@D)
	$(RASM) $< -ob $@

# Resident MOS directory/icon services and compact long-name browser.
build/agon/system.bin: kernel/agon/prefs.h
build/agon/files.bin: kernel/agon/files.c kernel/agon/prefs.c kernel/agon/prefs.h kernel/agon/viewport.c lib/gb/agon_viewport.h kernel/agon/icons.c kernel/agon/files_entry.s lib/gb/gb.h lib/gb/gblib.s
	mkdir -p $(@D)
	sdasz80 -o $(@D)/files_entry.rel kernel/agon/files_entry.s
	sdasz80 -o $(@D)/gblib.rel lib/gb/gblib.s
	sdcc -mz80 --fomit-frame-pointer -Ilib/gb -c $< -o $(@D)/files.rel
	sdcc -mz80 --opt-code-size -DGB_AGON -Ilib/gb -c kernel/agon/viewport.c -o $(@D)/viewport.rel
	sdcc -mz80 --opt-code-size -c kernel/agon/prefs.c -o $(@D)/prefs.rel
	sdcc -mz80 -c kernel/agon/icons.c -o $(@D)/icons.rel
	sdcc -mz80 --no-std-crt0 --code-loc 0x8800 --data-loc 0xf200 $(@D)/files_entry.rel $(@D)/files.rel $(@D)/icons.rel $(@D)/viewport.rel $(@D)/prefs.rel $(@D)/gblib.rel -o $(@D)/files.ihx
	python3 -c 'import re; s=open("$(@D)/files.map").read(); assert all(int(a,16)+int(n,16)<=0xf240 for a,n in re.findall(r"^_(?:DATA|INITIALIZED|BSS)\s+(\w+)\s+(\w+)",s,re.M)), "Agon Files data overlaps WM gestures"'
	makebin -s 65536 -p $(@D)/files.ihx $(@D)/files.flat
	tail -c +34817 $(@D)/files.flat > $@

# Only the boot icon set is a fixed kernel resource. Guests are in AGON.CAT.
build/agon/ist.inc: $(AGON_IST) tools/agon_signatures.py
	mkdir -p $(@D)
	python3 tools/agon_signatures.py $(AGON_IST) > $@

$(AGON_PREVIEW): kernel/agon/picview/main.c Makefile tools/build_capp.sh tools/build_cache.sh tools/check_app_layout.py lib/gb/gb.h lib/gb/gblib.s lib/gb/gbwin.c lib/gb/crt0.s
	APPDEFS=-DGB_AGON APP_CFLAGS=--fno-omit-frame-pointer GBWIN=1 bash tools/build_capp.sh kernel/agon/picview build/agon/PICVIEW.RAW
	mkdir -p $(@D)
	cp build/agon/PICVIEW.RAW $@

$(AGON_FM): apps/filemgr/main.c Makefile tools/build_capp.sh tools/build_cache.sh tools/check_app_layout.py lib/gb/gb.h lib/gb/gbwin.c lib/gb/gbdoc.c lib/gb/gbscroll.c lib/gb/gbui_stub.c lib/gb/gblib.s lib/gb/crt0.s
	APPDEFS=-DGB_AGON APP_CFLAGS=--opt-code-size DATA_LOC=0x7900 DIALOGS=1 SCROLL=1 bash tools/build_capp.sh apps/filemgr build/agon/FILEMGR.RAW
	mkdir -p $(@D)
	cp build/agon/FILEMGR.RAW $@

$(AGON_PREVIEW) $(AGON_VIEWER): lib/gb/agon_pic.h
$(AGON_VIEWER): apps/viewer/main.c apps/viewer/zoom.h apps/viewer/agon_bitmap.h apps/viewer/icon.asm Makefile tools/build_capp.sh tools/build_cache.sh tools/check_app_layout.py lib/gb/gb.h lib/gb/gbscroll16.c lib/gb/gbwin.c lib/gb/gbui_stub.c lib/gb/gblib.s lib/gb/crt0.s
	APPDEFS=-DGB_AGON APP_ICON=apps/viewer/icon.asm APP_CFLAGS=--fno-omit-frame-pointer DATA_LOC=0x6800 SCROLL16=1 PICKER=1 bash tools/build_capp.sh apps/viewer build/agon/VIEWER.APP
	mkdir -p $(@D)
	cp build/agon/VIEWER.APP $@

$(AGON_IST): QA/CPC/CARD/GBENCH/DEFAULT.IST
	mkdir -p $(@D)
	cp $< $@

$(AGON_CARD)/GBENCH/CALC.APP: $(AGON_CALC)
	mkdir -p $(@D)
	cp $< $@

$(AGON_CARD)/PICS/%.PIC: assets/pictures/%.PIC
	mkdir -p $(@D)
	cp $< $@

run-agon: agon
	mkdir -p $(AGON_RUN_CARD)
	cp -R $(AGON_CARD)/. $(AGON_RUN_CARD)/
	flatpak run --filesystem="$(abspath $(AGON_RUN_CARD)):rw" io.github.tomm.fab_agon_emulator --sdcard "$(abspath $(AGON_RUN_CARD))"

# Separate disposable card with timer telemetry and the non-yielding test guest.
AGON_DIAG := build/agon-task-sd
.PHONY: agon-taskdiag run-agon-taskdiag test-agon-taskdiag
build/agon/task_probe.bin: PROBE_FLAGS := -DAGON_TASK_PROBE=1
build/agon/worker.bin build/agon/task_probe.bin: kernel/agon/worker.asm kernel/agon/input.inc lib/gbapp.inc
	mkdir -p $(@D)
	$(RASM) $< $(PROBE_FLAGS) -ob $@

build/agon/TASKDEMO.APP: kernel/agon/taskdemo/main.c tools/build_capp.sh lib/gb/gb.h
	APPDEFS=-DGB_AGON TASK_STACK_RESERVE=512 GBWIN=1 bash tools/build_capp.sh kernel/agon/taskdemo $@

$(AGON_XAOS): apps/xaos/main.c apps/xaos/agon.h apps/xaos/icon.asm lib/gb/gb.h lib/gb/gbagon.h lib/gb/gbdoc.c lib/gb/gbwin.c lib/gb/gblib.s lib/gb/gbui_stub.c lib/gb/gbwidgets.c tools/build_capp.sh Makefile
	APPDEFS=-DGB_AGON APP_ICON=apps/xaos/icon.asm DATA_LOC=0x7000 TASK_STACK_RESERVE=512 DOC=1 BUTTON=1 bash tools/build_capp.sh apps/xaos build/agon/XAOS.APP
	cp build/agon/XAOS.APP $@

agon-taskdiag: agon build/agon/task_probe.bin build/agon/TASKDEMO.APP
	mkdir -p $(AGON_DIAG)
	cp -R $(AGON_CARD)/. $(AGON_DIAG)/
	$(RASM) kernel/agon/boot.asm -DAGON_TASK_PROBE=1 -ob $(AGON_DIAG)/GEOBENCH.BIN
	cp build/agon/TASKDEMO.APP $(AGON_DIAG)/
	python3 tools/agon_catalog.py $(AGON_DIAG)/GBENCH/AGON.CAT $(AGON_GUESTS) 4:$(AGON_DIAG)/TASKDEMO.APP

run-agon-taskdiag: agon-taskdiag
	flatpak run --filesystem="$(abspath $(AGON_DIAG)):rw" io.github.tomm.fab_agon_emulator --sdcard "$(abspath $(AGON_DIAG))"

test-agon-taskdiag: agon-taskdiag
	AGON_TASK_ONLY=1 xvfb-run -a -s "-screen 0 1024x768x24" python3 tools/test_agon_boot.py $(AGON_DIAG) $(AGON_TEST_HOST)

build/agon/test_glyphs: tools/test_agon_glyphs.c kernel/agon/icons.c
	mkdir -p $(@D)
	$(CC) -std=c99 -O2 $< -o $@

build/agon/test_saver_screen: tools/test_agon_saver_screen.c kernel/agon/saver_screen.c
	mkdir -p $(@D)
	$(CC) -std=c99 -O2 $^ -o $@

build/agon/test_window: tools/test_agon_window.c kernel/agon/gesture.c kernel/agon/geometry.h kernel/agon/window.c lib/gb/gb.h
	mkdir -p $(@D)
	$(CC) -std=c99 -O2 -Ilib/gb $< -o $@

test-agon: agon build/agon/test_glyphs build/agon/test_saver_screen build/agon/test_window
	build/agon/test_glyphs
	build/agon/test_saver_screen
	build/agon/test_window
	xvfb-run -a -s "-screen 0 1024x768x24" python3 tools/test_agon_boot.py $(AGON_CARD) $(AGON_TEST_HOST)

build/agon/system.bin build/agon/window.bin build/agon/files.bin $(AGON_XAOS) $(AGON_VIEWER) $(AGON_PREVIEW) build/agon/test_glyphs build/agon/test_window: lib/gb/agon_display.h

$(AGON_NOTE) $(AGON_FM) $(AGON_PREVIEW) $(AGON_VIEWER): lib/gb/agon_viewport.h lib/gb/agon_display.h

cpc:
	bash tools/build_kernel.sh

# Compatibility alias: preemptive scheduling is the release default.
cpc-preemptive: cpc

cpc-cooperative:
	PREEMPTIVE=0 bash tools/build_kernel.sh

msx:
	bash tools/build_kernel_msx.sh

# Compatibility alias: preemptive scheduling is the release default.
msx-preemptive: msx

msx-cooperative:
	PREEMPTIVE=0 bash tools/build_kernel_msx.sh

# Explicit scheduler stress image: stage and auto-open TASKDEMO workers.
msx-preemptive-diagnostic:
	PREEMPTIVE=1 PREEMPTIVE_DIAGNOSTIC=1 bash tools/build_kernel_msx.sh

msx-floppies:
	bash tools/build_msx_floppy.sh

pcw:
	bash tools/build_kernel_pcw.sh

# Compatibility alias: preemptive scheduling is the release default.
pcw-preemptive: pcw

pcw-cooperative:
	PREEMPTIVE=0 bash tools/build_kernel_pcw.sh

# Explicit scheduler stress image: stage and auto-open TASKDEMO workers.
pcw-preemptive-diagnostic:
	PREEMPTIVE=1 PREEMPTIVE_DIAGNOSTIC=1 bash tools/build_kernel_pcw.sh

app:
	@if [ -z "$(APP)" ]; then echo "usage: make app APP=mahjong"; exit 2; fi
	bash tools/rebuild_app.sh "$(APP)"

formref:
	APP_ICON=apps/formref/icon.asm APP_ICON16=apps/formref/icon16.asm DATA_LOC=0x6200 WIDGETS=1 STEPPER=1 SELECTOR=1 ACTIONS=1 FORM=1 FORM_SELECT=1 tools/build_capp.sh apps/formref build/FORMREF.RAW
	APP_ICON=apps/formref/icon.asm APP_ICON16=apps/formref/icon16.asm APPDEFS="-DGB_MSX2" DATA_LOC=0x6200 WIDGETS=1 STEPPER=1 SELECTOR=1 ACTIONS=1 FORM=1 FORM_SELECT=1 tools/build_capp.sh apps/formref build/msx/FORMREF.RAW
	APP_ICON=apps/formref/icon.asm APP_ICON16=apps/formref/icon16.asm APPDEFS="-DGB_PCW" DATA_LOC=0x6200 WIDGETS=1 STEPPER=1 SELECTOR=1 ACTIONS=1 FORM=1 FORM_SELECT=1 tools/build_capp.sh apps/formref build/pcw/FORMREF.RAW

sndtest:
	DATA_LOC=0x6200 BUTTON=1 SOUND=1 tools/build_capp.sh apps/sndtest build/SNDTEST.RAW
	APPDEFS="-DGB_MSX2" DATA_LOC=0x6200 BUTTON=1 SOUND=1 tools/build_capp.sh apps/sndtest build/msx/SNDTEST.RAW
	APPDEFS="-DGB_PCW" DATA_LOC=0x6200 BUTTON=1 SOUND=1 tools/build_capp.sh apps/sndtest build/pcw/SNDTEST.RAW

# Development-only preemption proof. TASKDEMO's worker never yields, so do not
# run it with an explicit cooperative kernel (PREEMPTIVE_CONTEXT=0).
taskdemo:
	TASK=1 TASK_STACK_RESERVE=256 DATA_LOC=0x6200 tools/build_capp.sh apps/taskdemo build/TASKDEMO.RAW
	TASK=1 TASK_STACK_RESERVE=256 APPDEFS="-DGB_MSX2" DATA_LOC=0x6200 tools/build_capp.sh apps/taskdemo build/msx/TASKDEMO.RAW
	TASK=1 TASK_STACK_RESERVE=256 APPDEFS="-DGB_PCW" DATA_LOC=0x6200 tools/build_capp.sh apps/taskdemo build/pcw/TASKDEMO.RAW

titlebar-editor:
	python3 tools/titlebaredit.py assets/titlebars/ORIGINAL.TBR assets/gadgets/ORIGINAL.GDT

check:
	git diff --check
	python3 tools/gen_pic_luts.py --check
	python3 tools/test_picconv.py
	python3 tools/png2mahjong.py --check assets/katakana.png assets/hiragana.png apps/mahjong/kana.h
	python3 tools/check_pic_distribution.py
	python3 tools/check_msx_floppies.py
	python3 tools/check_lowram_map.py
	python3 tools/check_lowram_map.py --profile msx
	python3 tools/check_abi_table.py
	python3 tools/test_app_layout.py
	python3 tools/test_appicon.py
	python3 tools/test_iconedit_tools.py
	python3 tools/test_titlebaredit.py
	bash apps/calculator/run_tests.sh
	kernel/kc/run_tests.sh
	lib/gb/run_tests.sh

test: check
