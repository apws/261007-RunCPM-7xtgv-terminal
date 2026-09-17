; ZOOM.COM -- set 7xtgv glyph zoom from CP/M.
;
; ZOOM [1|2|3|4] selects an absolute rendered glyph scale. No argument is
; 1x, the normal CP/M-compatible default. The private output event is consumed
; by current 7xtgv firmware; older firmware simply ignores it harmlessly.
;
; Build on CP/M:  ASM ZOOM
;                LOAD ZOOM
        org     0100h

        lda     0080h                   ; CCP command-tail length
        mov     b,a
        ora     a
        jz      zoom1                   ; no argument = normal 1x
        lxi     h,0081h

skipsp: mov     a,b
        ora     a
        jz      zoom1
        mov     a,m
        cpi     ' '
        jnz     argument
        inx     h
        dcr     b
        jmp     skipsp

argument:
        cpi     '1'
        jz      zoom1
        cpi     '2'
        jz      zoom2
        cpi     '3'
        jz      zoom3
        cpi     '4'
        jz      zoom4
        jmp     usage

zoom1:  lxi     d,one
        jmp     print
zoom2:  lxi     d,two
        jmp     print
zoom3:  lxi     d,three
        jmp     print
zoom4:  lxi     d,four

print:  mvi     c,9                     ; BDOS print-string
        call    5
        ret

usage:  lxi     d,help
        mvi     c,9
        call    5
        ret

one:    db      27,'[>68;1v','$'
two:    db      27,'[>68;2v','$'
three:  db      27,'[>68;3v','$'
four:   db      27,'[>68;4v','$'
help:   db      'ZOOM [1|2|3|4]',13,10,'$'
