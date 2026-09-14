; GCLS.COM -- request one local 7xtgv xterm-256 graphics transaction.
; The escape sequence is consumed by the terminal, then this transient CP/M
; command returns normally. It is useful from BROT-*.SUB scripts.

        org     0100h
        lxi     d,message
        mvi     c,9                    ; BDOS print-string
        call    5
        jmp     0

message: db      27,'[>4v','$'
