; VTGET filename: snapshot-safe 128-byte hex pages, Enter acknowledges each.
 org 0100h
 ld sp,stacktop
 ld de,005ch
 ld c,15
 call 5
 cp 255
 jp z,error
 ld de,0080h
 ld c,26
 call 5
readrec:
 ld de,005ch
 ld c,20
 call 5
 or a
 jr z,page
 cp 1
 jp nz,error
 ld de,done
 call puts
 ld hl,(record)
 call hexword
 ld de,newline
 call puts
 ld de,005ch
 ld c,16
 call 5
 jp 0
page:
 ld de,clear
 call puts
 ld de,heading
 call puts
 ld hl,(record)
 call hexword
 ld de,newline
 call puts
 ld hl,0080h
 ld b,4
row:
 push bc
 ld b,32
byte:
 ld a,(hl)
 call hexbyte
 inc hl
 djnz byte
 ld de,newline
 call puts
 pop bc
 djnz row
 ld de,ready
 call puts
 ld hl,(record)
 call hexword
 ld a,' '
 call putc
 call putname
 ld a,' '
 call putc
 ld de,progress
 call puts
 ld de,input
 ld c,10
 call 5
 ld a,(input+1)
 or a
 jr z,acknowledged
 cp 3
 jp nz,error
 ld a,(input+2)
 call pctdigit
 ld (progress),a
 ld a,(input+3)
 call pctdigit
 ld (progress+1),a
 ld a,(input+4)
 call pctdigit
 ld (progress+2),a
acknowledged:
 ld hl,(record)
 inc hl
 ld (record),hl
 jp readrec
record: dw 0
heading: db 'REC','$'
ready: db 'READY','$'
progress: db '000%','$'
done: db 13,10,'END','$'
pctdigit:
 cp '0'
 jp c,error
 cp ':'
 jp nc,error
 ret
input: db 4,0
 defs 5
 include 'common.inc'
 defs 128
stacktop:
