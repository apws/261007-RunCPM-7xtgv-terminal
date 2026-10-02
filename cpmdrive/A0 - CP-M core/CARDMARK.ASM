; CARDMARK -- native read-only SD diagnostic on supported 7xtgv firmware.
; Intel 8080, DRI ASM + LOAD. Native backend parses the command tail.
        org     0100h
        in      077h
        cpi     'C'
        jnz     absent
        mvi     a,1
        out     077h
        lxi     h,0080h
        mov     b,m
        inx     h
send:   mov     a,b
        ora     a
        jz      run
        mov     a,m
        out     078h
        inx     h
        dcr     b
        jmp     send
run:    mvi     a,2
        out     077h
        ret
absent: lxi     d,msg
        mvi     c,9
        call    5
        ret
msg:    db      ' CARDMARK: requires supported firmware',13,10,'$'
        end

