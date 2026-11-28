/* Opt-in drawing interface for byte-column, 16-bit-scanline client windows.
 * Small stamps still use the original ABI; change its origin only for the
 * duration of that synchronous draw, never across a poll or guest callback. */
#ifndef AGON_VIEWPORT_H
#define AGON_VIEWPORT_H
#include "agon_display.h"
#define ag_fill16 ((void (*)(unsigned char,unsigned,unsigned char,unsigned,unsigned char))0x090c)

/* Private jump table in the resident graphics/directory service. */
#define ag_stamp_y ((unsigned char (*)(unsigned))0x8830)
#define ag_text16 ((void (*)(unsigned char,unsigned,const char *))0x8833)
#define ag_textbw16 ((void (*)(unsigned char,unsigned,const char *))0x8836)
#define ag_frame16 ((void (*)(unsigned char,unsigned,unsigned char,unsigned char,unsigned char))0x8839)
#define ag_grip16 ((void (*)(unsigned char,unsigned,unsigned char,unsigned))0x883c)
#define ag_pointer_line ((unsigned (*)(void))0x883f)
#define gb_fill ag_fill16
#define gb_text ag_text16
#define gb_textbw ag_textbw16
#define gb_frame ag_frame16
#define gb_draw_grip ag_grip16
#define gb_my() ag_pointer_line()
#define gb_wm_h() AG_HEIGHT16(*(volatile unsigned char *)0x144b)
#endif
