; SPDX-License-Identifier: GPL-3.0-only
; video.asm -- framebuffer and terminal-board primitives for the P2000C
; (shared with p2000c-othello, plus tile copy, masking and a shifting blitter).
;
; The 512x252 high-res picture is composed in a 16128-byte framebuffer
; (64 bytes per line, line 0 at the top, MSB = leftmost dot) and sent to the
; terminal board with ESC r picture uploads, one per row of a rectangle. Two
; rules for those uploads were established on real hardware: the byte count
; must not have a zero low byte, and the terminal's picture RAM runs
; bottom-up (consecutive bytes of one upload continue on the line ABOVE), so
; a multi-line upload would have to be sent from its lowest line upward;
; this program only ever uploads single rows. All bytes go through BIOS
; CONOUT, because BDOS console output filters control characters and cannot
; send 0FFh.
;
; Calling convention (Z88DK, sdcc_iy): stack arguments are 16-bit words with
; the leftmost argument at SP+2 on entry; __z88dk_fastcall passes one word in
; HL; 8-bit results return in L. IX and IY are never touched.

SECTION code_user

PUBLIC _framebuffer
PUBLIC _video_clear
PUBLIC _video_fill
PUBLIC _video_or_col
PUBLIC _video_blit
PUBLIC _video_xor
PUBLIC _video_copy
PUBLIC _video_mask
PUBLIC _video_shift_or
PUBLIC _video_shift_xor
PUBLIC _entropy
PUBLIC _video_flush_rect
PUBLIC _video_graphics
PUBLIC _video_text
PUBLIC _conout
PUBLIC _conin
PUBLIC _conready
PUBLIC _con_puts
PUBLIC _con_at

defc FB_SIZE = 16128
defc LINE    = 64
defc ESC     = 27
defc BIOS    = 0x0001           ; -> WBOOT; CONIN = +9, CONOUT = +0Ch

; --- framebuffer ------------------------------------------------------------

; void video_clear(void)
_video_clear:
        ld hl,_framebuffer
        ld de,_framebuffer+1
        ld bc,FB_SIZE-1
        ld (hl),0
        ldir
        ret

; void video_fill(unsigned int offset, unsigned int value, unsigned int count)
; Sets count consecutive framebuffer bytes starting at offset to value.
_video_fill:
        ld hl,2
        add hl,sp
        ld c,(hl)
        inc hl
        ld b,(hl)               ; BC = offset
        inc hl
        ld e,(hl)               ; E = value
        inc hl
        inc hl
        ld a,(hl)
        inc hl
        ld h,(hl)
        ld l,a                  ; HL = count
        ld a,h
        or l
        ret z
        push hl
        ld hl,_framebuffer
        add hl,bc
        pop bc                  ; HL = destination, BC = count
fill_loop:
        ld (hl),e
        inc hl
        dec bc
        ld a,b
        or c
        jr nz,fill_loop
        ret

; void video_or_col(unsigned int offset, unsigned int mask, unsigned int count)
; ORs mask into one byte on each of count (1..255) consecutive lines.
_video_or_col:
        ld hl,2
        add hl,sp
        ld c,(hl)
        inc hl
        ld b,(hl)               ; BC = offset
        inc hl
        ld e,(hl)               ; E = mask
        inc hl
        inc hl
        ld d,(hl)               ; D = count
        ld hl,_framebuffer
        add hl,bc
        ld bc,LINE
orcol_loop:
        ld a,(hl)
        or e
        ld (hl),a
        add hl,bc
        dec d
        jr nz,orcol_loop
        ret

; Shared argument decoding for the blitters (called, so arguments sit at SP+4):
; HL = sprite, DE = framebuffer destination, B = rows, width stored aside.
blit_args:
        ld hl,4
        add hl,sp
        ld e,(hl)
        inc hl
        ld d,(hl)               ; DE = sprite
        inc hl
        ld c,(hl)
        inc hl
        ld b,(hl)               ; BC = offset
        inc hl
        ld a,(hl)
        ld (blit_width),a       ; bytes per sprite row
        inc hl
        ld a,(hl)               ; rows
        ld hl,_framebuffer
        add hl,bc
        ex de,hl
        ld b,a
        ret

; void video_blit(const unsigned char *sprite, unsigned int offset, unsigned int wh)
; ORs a sprite of (wh & 0xff) bytes per row and (wh >> 8) rows onto the framebuffer.
_video_blit:
        call blit_args
blit_row:
        push bc
        push de
        ld a,(blit_width)
        ld c,a
blit_byte:
        ld a,(de)
        or (hl)
        ld (de),a
        inc hl
        inc de
        dec c
        jr nz,blit_byte
        pop de
        ex de,hl
        ld bc,LINE
        add hl,bc
        ex de,hl
        pop bc
        djnz blit_row
        ret

; void video_xor(const unsigned char *sprite, unsigned int offset, unsigned int wh)
; Like video_blit but XORs, so drawing the same sprite twice removes it.
_video_xor:
        call blit_args
xor_row:
        push bc
        push de
        ld a,(blit_width)
        ld c,a
xor_byte:
        ld a,(de)
        xor (hl)
        ld (de),a
        inc hl
        inc de
        dec c
        jr nz,xor_byte
        pop de
        ex de,hl
        ld bc,LINE
        add hl,bc
        ex de,hl
        pop bc
        djnz xor_row
        ret

; void video_copy(const unsigned char *sprite, unsigned int offset, unsigned int wh)
; Like video_blit but replaces the framebuffer bytes (whole tiles).
_video_copy:
        call blit_args
copy_row:
        push bc
        push de
        ld a,(blit_width)
        ld c,a
        ld b,0
        ldir                    ; HL advances to the next sprite row
        pop de
        ex de,hl
        ld bc,LINE
        add hl,bc
        ex de,hl
        pop bc
        djnz copy_row
        ret

; void video_mask(const unsigned char *sprite, unsigned int offset, unsigned int wh)
; Clears the framebuffer dots that are set in the sprite (AND NOT).
_video_mask:
        call blit_args
mask_row:
        push bc
        push de
        ld a,(blit_width)
        ld c,a
mask_byte:
        ld a,(hl)
        cpl
        ex de,hl
        and (hl)
        ld (hl),a
        ex de,hl
        inc hl
        inc de
        dec c
        jr nz,mask_byte
        pop de
        ex de,hl
        ld bc,LINE
        add hl,bc
        ex de,hl
        pop bc
        djnz mask_row
        ret

; void video_shift_or(const unsigned char *sprite, unsigned int x, unsigned int line, unsigned int wh)
; void video_shift_xor(...)
; OR or XOR a sprite of (wh & 0xff) bytes per row and (wh >> 8) rows with its
; top-left dot at any dot position x (0..511) on the given line. Each row is
; shifted right by x & 7 in a buffer and lands in width + 1 framebuffer bytes,
; which must stay within the line (no clipping); width at most 15 bytes.
_video_shift_or:
        xor a
        jr shift_go
_video_shift_xor:
        ld a,1
shift_go:
        ld (shift_op),a
        ld hl,2
        add hl,sp
        ld e,(hl)
        inc hl
        ld d,(hl)               ; DE = sprite
        inc hl
        ld c,(hl)
        inc hl
        ld b,(hl)               ; BC = x
        inc hl
        ld a,(hl)               ; A = line
        inc hl
        inc hl
        push de
        ld e,(hl)               ; E = width in bytes
        inc hl
        ld d,(hl)               ; D = rows
        ld (shift_wh),de
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl               ; HL = line * 64
        ld a,c
        and 7
        ld (shift_count),a
        srl b
        rr c
        srl b
        rr c
        srl b
        rr c                    ; BC = x / 8
        add hl,bc
        ld bc,_framebuffer
        add hl,bc
        ex de,hl                ; DE = destination
        pop hl                  ; HL = sprite
shift_row:
        push de
        ld de,shift_buf         ; copy the row, plus a zero spill byte
        ld a,(shift_wh)
        ld c,a
        ld b,0
        ldir
        xor a
        ld (de),a
        push hl                 ; next sprite row
        ld a,(shift_count)
        or a
        jr z,shift_done
        ld c,a
shift_pass:
        ld hl,shift_buf
        ld a,(shift_wh)
        inc a
        ld b,a
        or a                    ; clear carry: a zero enters on the left
shift_byte:
        rr (hl)
        inc hl
        djnz shift_byte
        dec c
        jr nz,shift_pass
shift_done:
        pop bc                  ; BC = next sprite row
        pop de                  ; DE = destination of this row
        push bc
        push de
        ld hl,shift_buf
        ld a,(shift_wh)
        inc a
        ld b,a
        ld a,(shift_op)
        or a
        jr nz,shift_xor_byte
shift_or_byte:
        ld a,(de)
        or (hl)
        ld (de),a
        inc hl
        inc de
        djnz shift_or_byte
        jr shift_next
shift_xor_byte:
        ld a,(de)
        xor (hl)
        ld (de),a
        inc hl
        inc de
        djnz shift_xor_byte
shift_next:
        pop hl
        ld bc,LINE
        add hl,bc
        ex de,hl                ; DE = next destination
        pop hl                  ; HL = next sprite row
        ld a,(shift_wh+1)
        dec a
        ld (shift_wh+1),a
        jr nz,shift_row
        ret

; unsigned char entropy(void) -- the refresh register R, which advances with
; every instruction fetch; sampled at key presses it seeds the random numbers.
_entropy:
        ld a,r
        ld l,a
        ret

; void video_flush_rect(unsigned int col_row, unsigned int wh)
; Sends a rectangle of (wh & 0xff) bytes per row and (wh >> 8) rows whose
; top-left byte is column (col_row & 0xff) on line (col_row >> 8), as one
; short ESC r write per row. Width must be 1..255 bytes.
_video_flush_rect:
        ld hl,2
        add hl,sp
        ld c,(hl)               ; C = byte column
        inc hl
        ld b,(hl)               ; B = first line
        inc hl
        ld e,(hl)               ; E = width in bytes
        inc hl
        ld d,(hl)               ; D = rows
        ld a,d
        or a
        ret z
        ; HL = framebuffer + line * 64 + column
        ld a,b
        and 3
        rrca
        rrca
        ld l,a
        ld a,b
        rrca
        rrca
        and 0x3f
        ld h,a
        push bc
        ld b,0
        add hl,bc
        ld bc,_framebuffer
        add hl,bc
        pop bc
rect_row:
        push bc
        push de
        push hl
        ld a,ESC
        call conout_a
        ld a,'r'
        call conout_a
        ld a,c                  ; x = column * 8
        rlca
        rlca
        rlca
        and 0xf8
        call conout_a           ; x low
        ld a,c
        rrca
        rrca
        rrca
        rrca
        rrca
        and 0x07
        call conout_a           ; x high
        ld a,251
        sub b
        call conout_a           ; y = 251 - line
        ld a,e
        call conout_a           ; count low
        xor a
        call conout_a           ; count high
        pop hl
        push hl
        ld b,e
rect_byte:
        ld a,(hl)
        call conout_a
        inc hl
        djnz rect_byte
        pop hl
        ld bc,LINE
        add hl,bc
        pop de
        pop bc
        inc b                   ; next line
        dec d
        jr nz,rect_row
        ret

; --- terminal -----------------------------------------------------------------

; void video_graphics(void)  -- ESC 3 high-res mode, hide the blinking text cursor,
;                               home and clear the text plane
_video_graphics:
        ld hl,seq_graphics
        jr puts_loop

; void video_text(void)      -- ESC 4 back to the 80x24 character mode
_video_text:
        ld hl,seq_text
        jr puts_loop

; void con_puts(const char *s)   fastcall: zero-terminated, sent unfiltered
_con_puts:
puts_loop:
        ld a,(hl)
        or a
        ret z
        call conout_a
        inc hl
        jr puts_loop

; void con_at(unsigned int row_col)   fastcall: L = row, H = column (zero based)
_con_at:
        push hl
        ld a,ESC
        call conout_a
        ld a,'Y'
        call conout_a
        pop hl
        ld a,l
        add a,32
        call conout_a
        ld a,h
        add a,32
        jr conout_a

; void conout(unsigned int c)   fastcall: L = byte
_conout:
        ld a,l
; Sends A through BIOS CONOUT, preserving HL, DE and BC.
conout_a:
        push hl
        push de
        push bc
        ld c,a
        ld hl,(BIOS)
        ld l,0x0c
        call jphl
        pop bc
        pop de
        pop hl
        ret

; unsigned char conready(void)   -- nonzero when BIOS CONST reports a key waiting
_conready:
        ld hl,(BIOS)
        ld l,0x06
        call jphl
        ld l,a
        ret

; unsigned char conin(void)   -- blocks in BIOS CONIN, returns the byte in L
_conin:
        ld hl,(BIOS)
        ld l,0x09
        call jphl
        ld l,a
        ret

jphl:   jp (hl)

SECTION rodata_user

seq_graphics:
        defb ESC,'3',ESC,'c',ESC,'Y',32,32,ESC,'k',0      ; ESC c hides the text cursor
seq_text:
        defb ESC,'4',ESC,'C',0                             ; ESC C shows it again

SECTION bss_user

_framebuffer:
        defs FB_SIZE
blit_width:
        defs 1
shift_op:
        defs 1
shift_count:
        defs 1
shift_wh:
        defs 2
shift_buf:
        defs 16
