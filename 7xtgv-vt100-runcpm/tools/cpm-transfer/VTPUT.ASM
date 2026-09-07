; VTPUT filename: four requested 32-byte hex lines per disk record.
; A single ! line at a record boundary closes the file.
 org 0100h
 ld sp,stacktop
 ld de,005ch
 ld c,19
 call 5
 ld de,005ch
 ld c,22
 call 5
 cp 255
 jp z,error
 ld de,0080h
 ld c,26
 call 5
newrec:
 ld hl,0080h
 ld b,4
chunk:
 push bc
 push hl
 ld de,clear
 call puts
 ld de,heading
 call puts
 ld hl,(counter)
 call hexword
 ld a,':'
 call putc
 ld de,input
 ld c,10
 call 5
 ld a,(input+1)
 cp 1
 jr nz,decode
 ld a,(input+2)
 cp '!'
 jp nz,error
 pop hl
 pop bc
 ld a,b
 cp 4
 jp nz,error
 ld de,005ch
 ld c,16
 call 5
 cp 255
 jp z,error
 ld de,done
 call puts
 jp 0
decode:
 cp 64
 jp nz,error
 pop de
 ld hl,input+2
 ld b,32
pair:
 call nibble
 rlca
 rlca
 rlca
 rlca
 ld c,a
 call nibble
 or c
 ld (de),a
 inc de
 djnz pair
 ld hl,(counter)
 inc hl
 ld (counter),hl
 ex de,hl
 pop bc
 djnz chunk
 ld de,005ch
 ld c,21
 call 5
 or a
 jp nz,error
 jp newrec
nibble:
 ld a,(hl)
 inc hl
 sub 48
 jp c,error
 cp 10
 ret c
 sub 7
 cp 10
 jp c,error
 cp 16
 jp nc,error
 ret
counter: dw 0
heading: db 'PUT','$'
done: db 13,10,'SAVED',13,10,'$'
input: db 65,0
 defs 66
 include 'common.inc'
 defs 128
stacktop:
