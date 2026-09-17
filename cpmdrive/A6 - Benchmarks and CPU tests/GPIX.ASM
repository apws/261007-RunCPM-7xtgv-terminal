; GPIX.COM -- small xterm-256 plot sample stream for GCLS testing.
;
; Run after GCLS has armed a transaction, for example:
;   GCLS 2 20 20
;   GPIX
; It emits a solid 10x10 logical-pixel red square, then CSI 0m.  At 2x it is
; a clearly visible 20x20 physical-pixel square.  Without a working GCLS the
; bounded failure is 100 short "1" samples, never a long renderer stream.
;
; A plot SGR (38;5;n m) arms *one* following byte as a pixel. Therefore each
; sample deliberately has its own SGR; sending one SGR then ten bytes would
; turn the final nine bytes into ordinary terminal text.

        org     0100h

        mvi     b,10                    ; ten logical rows
row:    push    b
        mvi     b,10                    ; ten logical samples per row
pixel:  push    b
        lxi     d,redsample
        mvi     c,9                     ; BDOS print-string
        call    5
        pop     b
        dcr     b
        jnz     pixel
        lxi     d,rowend
        mvi     c,9
        call    5
        pop     b
        dcr     b
        jnz     row
        lxi     d,plotend
        mvi     c,9
        call    5
        jmp     0

; Every stream byte is decimal. This avoids assembler quotation edge cases.
; ESC [ 38;5;196 m 1 ; CR LF ; ESC [ 0 m ; CP/M '$' terminator.
redsample: db    27,91,51,56,59,53,59,49,57,54,109,49,36
rowend:    db    13,10,36
plotend:   db    27,91,48,109,36
