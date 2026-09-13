; VTPAD.COM [count] -- emit count CR/LF pairs (default 18).
; CP/M SUBMIT may discard empty source records, so use explicit console output.

        org     0100h

bdos    equ     5

start:  mvi     a,18             ; default when no decimal argument is present
        sta     remain
        lxi     h,0081h          ; command tail (length byte at 80h)
skipsp: mov     a,m
        cpi     ' '
        jnz     first
        inx     h
        jmp     skipsp
first:  cpi     13
        jz      emit
        mvi     a,0
        sta     remain
digit:  mov     a,m
        cpi     '0'
        jc      emit
        cpi     ':'
        jnc     emit
        sui     '0'
        mov     d,a
        lda     remain
        add     a                ; 2*n
        mov     e,a
        add     a                ; 4*n
        add     a                ; 8*n
        add     e                ; 10*n
        add     d                ; 10*n + digit, modulo 256
        sta     remain
        inx     h
        jmp     digit
emit:   lda     remain
        ora     a
        rz
        mvi     e,13
        mvi     c,2
        call    bdos
        mvi     e,10
        mvi     c,2
        call    bdos
        lda     remain
        dcr     a
        sta     remain
        jmp     emit
        ret

remain: db      0
        end     start
