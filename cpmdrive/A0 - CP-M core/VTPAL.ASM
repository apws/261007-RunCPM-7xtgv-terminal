; VTPAL [0..9] -- select 7xtgv text/UI palette. No argument selects 0.
; Requires firmware implementing output CSI >69;n v.
; Build: ASM VTPAL followed by LOAD VTPAL. Intel 8080 / Digital Research ASM.
        org     0100h
        mvi     a,'0'
        sta     choice
        lda     0080h
        mov     b,a
        lxi     h,0081h
skipsp: mov     a,b
        ora     a
        jz      emit
        mov     a,m
        cpi     ' '
        jnz     digit
        inx     h
        dcr     b
        jmp     skipsp
digit:  cpi     '0'
        jc      usage
        cpi     ':'
        jnc     usage
        sta     choice
        inx     h
        dcr     b
tailsp: mov     a,b
        ora     a
        jz      emit
        mov     a,m
        cpi     ' '
        jnz     usage
        inx     h
        dcr     b
        jmp     tailsp
emit:   lxi     d,seq
        jmp     print
usage:  lxi     d,help
print:  mvi     c,9
        call    5
        ret
seq:    db      27,'[>69;'
choice: db      '0','v','$'
help:   db      'VTPAL [0..9]',13,10,'$'
        end
