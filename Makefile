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
AGON_PICS := $(patsubst assets/pictures/%,$(AGON_CARD)/PICS/%,$(wildcard assets/pictures/*.PIC))
agon: $(AGON_CARD)/GEOBENCH.BIN $(AGON_PICS) $(AGON_CARD)/GBENCH/CALC.APP $(AGON_FM) $(AGON_IST)

$(AGON_CARD)/GEOBENCH.BIN: kernel/agon/boot.asm kernel/agon/input.asm kernel/agon/input.inc kernel/lowram.inc lib/gbapp.inc build/agon/runtime.bin build/agon/files.bin build/agon/window.bin build/agon/system.bin build/agon/calc.inc
	mkdir -p $(@D)
	$(RASM) $< -ob $@

# Same analog face and Squares animation as the other targets; MOS input,
# persistence and VDP line drawing are Agon-specific.
build/agon/system.bin: kernel/agon/system.c kernel/agon/saver_screen.c kernel/agon/system_entry.s apps/clock/main.c apps/saver/main.c lib/gb/gbwin.c lib/gb/gblib.s lib/gb/gb.h
	mkdir -p $(@D)
	sdasz80 -o $(@D)/system_entry.rel kernel/agon/system_entry.s
	sdasz80 -o $(@D)/syslib.rel lib/gb/gblib.s
	sdcc -mz80 --opt-code-size -DGB_AGON -Ilib/gb -c kernel/agon/system.c -o $(@D)/sys.rel
	sdcc -mz80 --opt-code-size -c kernel/agon/saver_screen.c -o $(@D)/saver_screen.rel
	sdcc -mz80 --opt-code-size -DGB_AGON -Ilib/gb -c apps/clock/main.c -o $(@D)/clock.rel
	sdcc -mz80 --opt-code-size -DGB_AGON -Ilib/gb -c apps/saver/main.c -o $(@D)/squares.rel
	sdcc -mz80 --opt-code-size -DGB_AGON -Ilib/gb -c lib/gb/gbwin.c -o $(@D)/syswin.rel
	sdcc -mz80 --no-std-crt0 --code-loc 0xa000 --data-loc 0xd800 $(addprefix $(@D)/,system_entry.rel sys.rel saver_screen.rel clock.rel squares.rel syswin.rel syslib.rel) -o $(@D)/system.ihx
	makebin -s 65536 -p $(@D)/system.ihx $(@D)/system.flat
	tail -c +40961 $(@D)/system.flat > $@

build/agon/window.bin: kernel/agon/window.c kernel/agon/window_entry.s lib/gb/gb.h
	mkdir -p $(@D)
	sdasz80 -o $(@D)/window_entry.rel kernel/agon/window_entry.s
	sdcc -mz80 --opt-code-size -DGB_AGON -Ilib/gb -c $< -o $(@D)/window.rel
	sdcc -mz80 --no-std-crt0 --code-loc 0x0900 --data-loc 0x1e00 $(@D)/window_entry.rel $(@D)/window.rel -o $(@D)/window.ihx
	makebin -s 65536 -p $(@D)/window.ihx $(@D)/window.flat
	tail -c +2305 $(@D)/window.flat > $@

build/agon/runtime.bin: kernel/agon/runtime.asm kernel/agon/draw.asm kernel/agon/input.inc lib/gbapp.inc kernel/lowram.inc QA/CPC/CARD/GBENCH/DEFAULT.FNT
	mkdir -p $(@D)
	$(RASM) $< -ob $@

# Resident MOS directory/icon services and compact long-name browser.
build/agon/files.bin: kernel/agon/files.c kernel/agon/icons.c kernel/agon/files_entry.s lib/gb/gb.h lib/gb/gblib.s
	mkdir -p $(@D)
	sdasz80 -o $(@D)/files_entry.rel kernel/agon/files_entry.s
	sdasz80 -o $(@D)/gblib.rel lib/gb/gblib.s
	sdcc -mz80 --fomit-frame-pointer -Ilib/gb -c $< -o $(@D)/files.rel
	sdcc -mz80 -c kernel/agon/icons.c -o $(@D)/icons.rel
	sdcc -mz80 --no-std-crt0 --code-loc 0x8800 --data-loc 0xf200 $(@D)/files_entry.rel $(@D)/files.rel $(@D)/icons.rel $(@D)/gblib.rel -o $(@D)/files.ihx
	makebin -s 65536 -p $(@D)/files.ihx $(@D)/files.flat
	tail -c +34817 $(@D)/files.flat > $@

# The guests have no executable header. Only enter the staged payloads.
build/agon/calc.inc: $(AGON_CALC) $(AGON_FM) $(AGON_IST) $(AGON_PREVIEW) tools/agon_signatures.py
	mkdir -p $(@D)
	python3 tools/agon_signatures.py $(AGON_CALC) $(AGON_FM) $(AGON_IST) $(AGON_PREVIEW) > $@

$(AGON_PREVIEW): kernel/agon/picview/main.c Makefile tools/build_capp.sh tools/build_cache.sh tools/check_app_layout.py lib/gb/gb.h lib/gb/gblib.s lib/gb/gbwin.c lib/gb/crt0.s
	APPDEFS=-DGB_AGON APP_CFLAGS=--fno-omit-frame-pointer GBWIN=1 bash tools/build_capp.sh kernel/agon/picview build/agon/PICVIEW.RAW
	mkdir -p $(@D)
	cp build/agon/PICVIEW.RAW $@

$(AGON_FM): apps/filemgr/main.c Makefile tools/build_capp.sh tools/build_cache.sh tools/check_app_layout.py lib/gb/gb.h lib/gb/gbwin.c lib/gb/gbdoc.c lib/gb/gbscroll.c lib/gb/gbui_stub.c lib/gb/gblib.s lib/gb/crt0.s
	APPDEFS=-DGB_AGON APP_CFLAGS=--opt-code-size DATA_LOC=0x7800 DOCRO=1 SCROLL=1 bash tools/build_capp.sh apps/filemgr build/agon/FILEMGR.RAW
	mkdir -p $(@D)
	cp build/agon/FILEMGR.RAW $@

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

build/agon/test_glyphs: tools/test_agon_glyphs.c kernel/agon/icons.c
	mkdir -p $(@D)
	$(CC) -std=c99 -O2 $< -o $@

build/agon/test_saver_screen: tools/test_agon_saver_screen.c kernel/agon/saver_screen.c
	mkdir -p $(@D)
	$(CC) -std=c99 -O2 $^ -o $@

build/agon/test_window: tools/test_agon_window.c kernel/agon/window.c lib/gb/gb.h
	mkdir -p $(@D)
	$(CC) -std=c99 -O2 -Ilib/gb $< -o $@

test-agon: agon build/agon/test_glyphs build/agon/test_saver_screen build/agon/test_window
	build/agon/test_glyphs
	build/agon/test_saver_screen
	build/agon/test_window
	xvfb-run -a -s "-screen 0 1024x768x24" python3 tools/test_agon_boot.py $(AGON_CARD) $(AGON_TEST_HOST)

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
