#ifndef AGON_PREFS_H
#define AGON_PREFS_H
/* GBA1 byte 6: old files used only SECONDS, so they select 320x240. */
typedef struct { unsigned char saver, timeout, flags; } Prefs;
enum { SECONDS=1, VGA=2 };
#define prefs_io ((unsigned char (*)(unsigned char,Prefs *))0x8842)
#endif
