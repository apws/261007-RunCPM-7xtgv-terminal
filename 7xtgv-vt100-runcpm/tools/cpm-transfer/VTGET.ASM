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
 ld de,input
 ld c,10
 call 5
 ld hl,(record)
 inc hl
 ld (record),hl
 jr readrec
record: dw 0
heading: db 'REC','$'
ready: db 'READY','$'
done: db 13,10,'END','$'
input: db 1,0,0,0
 include 'common.inc'
 defs 128
stacktop:
