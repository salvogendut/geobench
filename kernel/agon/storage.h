#ifndef AGON_STORAGE_H
#define AGON_STORAGE_H
typedef unsigned char u8;
extern u8 ag_handle;
u8 ag_read_open(const char *), ag_new(const char *), ag_close(u8);
u8 ag_rename(const char *,const char *), ag_delete(const char *), ag_mkdir(const char *);
u8 ag_stat(void *,const char *);
unsigned ag_read(void *,unsigned), ag_write(const void *,unsigned);
u8 ag_join(char *out,const char *dir,const char *leaf);
u8 ag_within(const char *path,const char *ancestor);
/* All paths are in the same directory. NULL backup means no overwrite. */
u8 ag_publish(const char *stage,const char *dest,const char *backup);
/* Shared only by synchronous root filesystem operations. */
extern __at(0x3600) u8 ag_info[288];
#endif
