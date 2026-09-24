; UNSUB.COM -- remove a stranded CP/M SUBMIT queue from the current DU.
; The zero-drive FCB and current BDOS user are intentionally left unchanged.

        org     0100h

bdos    equ     5
delete  equ     19

start:  lxi     d,fcb
        mvi     c,delete
        call    bdos
        ret

fcb:    db      0,'$$$     ','SUB'
        ds      24

        end     start
