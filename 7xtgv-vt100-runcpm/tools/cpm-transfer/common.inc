; Register-preserving console helpers.
puts:
 push af
 push bc
 push de
 push hl
 ld c,9
 call 5
 pop hl
 pop de
 pop bc
 pop af
 ret
putc:
 push af
 push bc
 push de
 push hl
 ld e,a
 ld c,2
 call 5
 pop hl
 pop de
 pop bc
 pop af
 ret
hexbyte:
 push af
 rrca
 rrca
 rrca
 rrca
 call digit
 pop af
 call digit
 ret
digit:
 and 15
 add a,48
 cp 58
 jr c,digitout
 add a,7
digitout:
 jp putc
hexword:
 ld a,h
 call hexbyte
 ld a,l
 jp hexbyte
clear:
 db 27,'(B',15,27,'[0m',27,'[2J',27,'[H','$'
newline:
 db 13,10,'$'
error:
 ld de,errtext
 call puts
 jp 0
errtext:
 db 13,10,'XFER ERROR',13,10,'$'
