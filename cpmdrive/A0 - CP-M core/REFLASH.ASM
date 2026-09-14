; REFLASH.COM -- request an opt-in host firmware reflash from 7xtgv terminal.py.
;
; First forces the same verified history save as SYNC.COM (port 7Dh).  Only
; after that succeeds does it emit the reserved guest-to-host event ESC[>64v.
; It does not reboot the MCU and does not itself alter firmware. The PC bridge
; acts only when its XTGV_REFLASH_COMMAND is configured.
;
; Build on CP/M:  ASM REFLASH
;                LOAD REFLASH
        org     0100h

        mvi     a,1
        out     07dh
        in      07dh
        cpi     1
        jnz     failed

        lxi     d,request
        mvi     c,9
        call    5
        ret

failed: lxi     d,errmsg
        mvi     c,9
        call    5
        ret

request:
        db      27,'[>64v','$'
errmsg: db      'REFLASH: history save failed',13,10,'$'
