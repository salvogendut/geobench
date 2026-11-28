#ifndef AGON_GEOMETRY_H
#define AGON_GEOMETRY_H
typedef unsigned char u8;
/* Column units horizontally, physical scanlines vertically. */
typedef struct { u8 x,w; unsigned y,h; } AgBox;
extern u8 win_poll(void);
extern void win_outline(const AgBox *);
extern u8 win_gesture(AgBox *,const u8 *,u8);
#endif
