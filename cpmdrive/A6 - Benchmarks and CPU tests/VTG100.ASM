; VTG100.COM -- fixed GCLS protocol probe, independent of argument parsing.
; Emits only ESC[>4;2;100;100v, then returns to CCP.

        org     0100h
        lxi     d,control
        mvi     c,9
        call    5
        jmp     0

control: db     27,91,62,52,59,50,59,49,48,48,59,49,48,48,118,36
