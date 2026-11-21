/* From a built 1983 checkout with its NMS8250 ROMs and test-msx objects:
 * cc -Isrc ../geobench/debug/msx_cursor_probe.c src/test_msx-*.o \
 *    -Wl,--wrap=vdp_write_data -lm -o /tmp/cursor-probe
 * /tmp/cursor-probe /path/to/GBMSX.IMG 7   (or 6; match MSXMODE in the image)
 */
#include "msx.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static MsxMachine machine;
static unsigned base, reveals, hidden_frames;
static int armed;

static u8 attr(unsigned offset) {
    unsigned addr = base + offset;
    if (base == 0xfa00) addr = (addr >> 1) | ((addr & 1) << 16);
    return machine.vdp.vram[addr];
}

void __real_vdp_write_data(MsxVdp *, u8);
void __wrap_vdp_write_data(MsxVdp *vdp, u8 value) {
    unsigned addr = ((vdp->registers[14] & 7) << 14) | (vdp->address & 0x3fff);
    if (machine.cpu.pc >= 0x8000 && machine.cpu.pc < 0xc000 &&
        addr >= base && addr < base + 9) {
        assert(!vdp->cpu_vram_pending);  /* another OUT would drop a byte */
        if (addr == base) {
            if (!armed) assert(value == 216);
            armed = 1;
            if (value != 216) {
                assert(value == attr(4) && attr(1) == attr(5));
                assert(attr(2) == 0 && attr(6) == 4 && attr(8) == 216);
                ++reveals;
            }
        } else {
            assert(attr(0) == 216);      /* never show a half-updated pair */
        }
    }
    __real_vdp_write_data(vdp, value);
}

int main(int argc, char **argv) {
    assert(argc == 3 && (atoi(argv[2]) == 6 || atoi(argv[2]) == 7));
    base = atoi(argv[2]) == 7 ? 0xfa00 : 0x7600;
    msx_init(&machine, MSX_MODEL_PHILIPS_NMS8250, MSX_REGION_PAL, 512);
    MsxFloppyConfig floppy = {.controller = MSX_FLOPPY_CONTROLLER_PHILIPS_WD2793,
                              .primary_slot = 3, .secondary_slot = 3};
    assert(!msx_configure_floppy(&machine, &floppy));
    assert(!msx_load_firmware_set(&machine, "ROMS/nms8250_basic-bios2.rom", NULL,
                                  "ROMS/nms8250_msx2sub.rom", "ROMS/nms8250_disk.rom"));
    assert(!msx_load_sd_mapper(&machine, 1, "ROMS/SDM V2 Nextor2.1.1.rom"));
    assert(!msx_mount_sd_card(&machine, 0, argv[1], SD_IMAGE_READ_ONLY));
    msx_reset(&machine);
    msx_mouse_set_enabled(&machine, 0, true);
    u8 *low = machine.sd_mapper.ram + 3 * 16384;
    for (int i = 0; i < 1900; ++i) {
        if (i >= 1300 && i < 1330 && i % 3 == 0) {
            int dx = 16 - 4 * low[0x1306], dy = 36 - low[0x1307];
            msx_mouse_add_host_motion(&machine, 0, 2 * (dx < -40 ? -40 : dx),
                                      2 * (dy < -40 ? -40 : dy));
        }
        if (i == 1340 || i == 1350) msx_mouse_set_button(&machine, 0, 0, true);
        if (i == 1343 || i == 1353) msx_mouse_set_button(&machine, 0, 0, false);
        if (i >= 1600 && i % 7 == 0)
            msx_mouse_add_host_motion(&machine, 0, i % 2 ? 17 : -11, i % 2 ? 7 : -5);
        msx_run_frame(&machine);
        if (armed && !reveals && attr(0) == 216) ++hidden_frames;
    }
    assert(low[0x1350] >= 2);           /* File Manager opened */
    assert(hidden_frames > 5 && reveals > 20 && attr(0) != 216);
    printf("Screen %s: hidden for %u boot frames; %u coherent reveals\n",
           argv[2], hidden_frames, reveals);
    msx_destroy(&machine);
}
