/* Agon-private display state, initialised by the MOS launcher (input.inc).
 * Guests keep byte Y coordinates; the active window owns its physical offset. */
#ifndef AGON_DISPLAY_H
#define AGON_DISPLAY_H
static __at(0x17eb) volatile unsigned char ag_vga;
static __at(0x17ec) volatile unsigned char ag_ybase;
static __at(0x17ed) volatile unsigned ag_mouse_y;
static __at(0x17e7) volatile unsigned char ag_hhi;
#define AG_HEIGHT16(lo) (((unsigned)ag_hhi << 8) | (lo))
#define AG_PHYS_Y(y) ((unsigned)(y) + ag_ybase)
#endif
