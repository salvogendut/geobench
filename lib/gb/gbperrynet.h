/* gbperrynet.h - shared PCW PerryNet GEOBENCH.CFG helpers. */
#ifndef GBPERRYNET_H
#define GBPERRYNET_H

#define GB_PERRYNET_BAUD_KEY      "PERRYNET_BAUD="
#define GB_PERRYNET_BAUD_DEFAULT  17857
#define GB_PERRYNET_BAUD_MIN      9600
#define GB_PERRYNET_BAUD_MAX      41667

#define GB_PERRYNET_PROFILE_9600  0
#define GB_PERRYNET_PROFILE_17857 1
#define GB_PERRYNET_PROFILE_41667 2

static unsigned char gb_perrynet_profile(void) __naked
{
__asm
    ld e,#1                         ; default: 17857 profile
    ld hl,#0x1000                   ; KCFG_TEXT
    ld bc,(#0x1200)                 ; KCFG_LEN
gbpn_line_start:
    ld a,b
    or c
    jr z,gbpn_done
    push hl
    push bc
    ld de,#gbpn_key
gbpn_match:
    ld a,(de)
    or a
    jr z,gbpn_matched
    ld a,b
    or c
    jr z,gbpn_no_match
    ld a,(de)
    cp (hl)
    jr nz,gbpn_no_match
    inc de
    inc hl
    dec bc
    jr gbpn_match
gbpn_matched:
    ld a,(hl)
    ld e,#1
    cp #0x39                       ; '9' -> 9600
    jr nz,gbpn_not_9600
    ld e,#0
    jr gbpn_no_match
gbpn_not_9600:
    cp #0x33                       ; '3' -> 38400/41667
    jr z,gbpn_fastest
    cp #0x34
    jr nz,gbpn_no_match
gbpn_fastest:
    ld e,#2
gbpn_no_match:
    pop bc
    pop hl
gbpn_skip:
    ld a,b
    or c
    jr z,gbpn_done
    ld a,(hl)
    inc hl
    dec bc
    cp #0x0a
    jr z,gbpn_eol
    cp #0x0d
    jr nz,gbpn_skip
gbpn_eol:
    ld a,b
    or c
    jr z,gbpn_done
    ld a,(hl)
    cp #0x0a
    jr z,gbpn_consume_nl
    cp #0x0d
    jr nz,gbpn_line_start
gbpn_consume_nl:
    inc hl
    dec bc
    jr gbpn_eol
gbpn_done:
    ld a,e
    ret
gbpn_key:
    .ascii "PERRYNET_BAUD="
    .db 0
__endasm;
}

#endif
