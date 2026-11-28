#ifndef GB_AGON_SERVICES_H
#define GB_AGON_SERVICES_H
/* Agon MOS guest extensions, SDCC Z80 calling convention. A worker must
 * reserve the top 512 bytes of its guest page and use only pure computation.
 * Returning ends the job. Start returns zero if the timer is unavailable.
 * The optional byte in guest RAM is sampled for the diagnostic counter. */
#define gb_agon_worker_start ((unsigned char (*)(void (*)(void), volatile unsigned char *))0xa045)
#define gb_agon_worker_cancel ((void (*)(void))0xa048)
/* Choose a destination and export a complete GBPC without changing the
 * document's save target. Cancel/failure returns zero. */
#define gb_agon_export_pic ((unsigned char (*)(const void *,unsigned))0xa04b)
typedef unsigned char (*gb_agon_validator)(const void *,unsigned);
#define gb_agon_validate_next ((void (*)(gb_agon_validator))0xa04e)
#endif
