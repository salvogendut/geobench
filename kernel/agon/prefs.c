#include "prefs.h"
#include <string.h>
typedef unsigned char u8;
extern u8 cfg_open(const char *), cfg_create(const char *), cfg_close(u8);
extern unsigned cfg_read(u8 *), cfg_write(const u8 *);
u8 cfg_handle;

/* Only publish checked values to the caller, including for a short read. */
u8 ag_preferences(u8 write, Prefs *prefs)
{
    static const char path[]="/GBENCH/AGON.CFG";
    u8 p[8], ok;
    cfg_handle=write ? cfg_create(path) : cfg_open(path);
    if (!cfg_handle) return 0;
    if (write) {
        memcpy(p,"GBA1",4);
        memcpy(p+4,prefs,3);
        p[7]=0xa5 ^ p[4] ^ p[5] ^ p[6];
        ok=cfg_write(p)==sizeof p;
    } else {
        ok=cfg_read(p)==sizeof p && !memcmp(p,"GBA1",4) &&
            p[4]<2 && p[5]<5 && p[6]<4 && p[7]==(u8)(0xa5 ^ p[4] ^ p[5] ^ p[6]);
        if (ok) memcpy(prefs,p+4,3);
    }
    cfg_close(cfg_handle);
    return ok;
}
