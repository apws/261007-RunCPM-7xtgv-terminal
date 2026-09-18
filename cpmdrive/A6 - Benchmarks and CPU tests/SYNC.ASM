; SYNC.COM -- synchronously persist terminal history and changed INI settings.
; Port 7Dh returns 1 only after both required verified transactions succeed.

        org     0100h

port    equ     07dh
bdos    equ     5

start:  mvi     a,1
        out     port
        in      port
        cpi     1
        rz

        lxi     d,errmsg
        mvi     c,9
        call    bdos
        ret

errmsg: db      'SYNC: history save failed',13,10,'$'
        end     start
