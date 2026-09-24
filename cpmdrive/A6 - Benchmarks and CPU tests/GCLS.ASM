; GCLS.COM -- initialise one local 7xtgv xterm-256 graphics transaction.
;
; GCLS [zoom [x y]] -- zoom is 1..4; x/y are physical panel pixels.
; No arguments retain the original ESC[>4v command. Positioned/scaled variants
; emit ESC[>4;zoom;x;yv. The escape sequence is consumed by current 7xtgv
; firmware, then this transient CP/M command returns normally. It is useful
; from BROT-*.SUB scripts.

        org     0100h

        lda     0080h                   ; CCP command-tail length
        sta     remaining
        lxi     h,0081h
        call    skipsp
        jz      bare

        call    token
        call    validzoom
        lhld    tokenptr
        shld    zoomptr
        lda     tokenlen
        sta     zoomlen
        lhld    endptr
        call    skipsp
        jz      emit

        call    token
        call    validcoord
        lhld    tokenptr
        shld    xptr
        lda     tokenlen
        sta     xlen
        lhld    endptr
        call    skipsp
        jz      emit

        call    token
        call    validcoord
        lhld    tokenptr
        shld    yptr
        lda     tokenlen
        sta     ylen
        lhld    endptr
        call    skipsp
        jnz     usage

emit:   lxi     d,prefix
        mvi     c,9                    ; BDOS print-string
        call    5
        lhld    zoomptr
        lda     zoomlen
        mov     b,a
        call    printtoken
        mvi     e,';'
        mvi     c,2
        call    5
        lhld    xptr
        lda     xlen
        mov     b,a
        call    printtoken
        mvi     e,';'
        mvi     c,2
        call    5
        lhld    yptr
        lda     ylen
        mov     b,a
        call    printtoken
        mvi     e,'v'
        mvi     c,2
        call    5
        jmp     0

bare:   lxi     d,baremsg
        mvi     c,9
        call    5
        jmp     0

; Skip ordinary CP/M command-tail spaces. HL remains at the first argument;
; Z means no argument remains.
skipsp: lda     remaining
        ora     a
        rz
        mov     a,m
        cpi     ' '
        jz      skipspace
        cpi     00h                     ; some CCPs NUL-terminate the tail
        jz      tailend
        cpi     0ah                     ; tolerate LF from host bridges
        jz      tailend
        cpi     0dh                     ; CP/M command tails end in CR
        jnz     skipdone
tailend:
        xra     a
        sta     remaining
        ret
skipspace:
        inx     h
        lda     remaining
        dcr     a
        sta     remaining
        jmp     skipsp
skipdone:
        ret

; Capture one decimal token. Input HL must point to a non-space byte.
token:  shld    tokenptr
        xra     a
        sta     tokenlen
tokloop: lda    remaining
        ora     a
        jz      tokend
        mov     a,m
        cpi     ' '
        jz      tokend
        cpi     '0'
        jc      usage
        cpi     ':'
        jnc     usage
        lda     tokenlen
        cpi     4
        jnc     usage
        inr     a
        sta     tokenlen
        inx     h
        lda     remaining
        dcr     a
        sta     remaining
        jmp     tokloop
tokend: shld    endptr
        ret

validzoom:
        lda     tokenlen
        cpi     1
        jnz     usage
        lhld    tokenptr
        mov     a,m
        cpi     '1'
        jc      usage
        cpi     '5'
        jnc     usage
        ret

validcoord:
        lda     tokenlen
        ora     a
        jz      usage
        ret

; RunCPM BDOS resets HL on entry. Preserve both the source pointer and count
; across every console-output call, especially for multi-digit coordinates.
printtoken:
        mov     a,b
        ora     a
        rz
        mov     e,m
        push    b
        push    h
        mvi     c,2
        call    5
        pop     h
        pop     b
        inx     h
        dcr     b
        jmp     printtoken

; A malformed/incomplete tail must never leave a following xterm-pixel stream
; in ordinary text mode. Fall back to the original 1x GCLS control silently.
usage:  jmp     bare

baremsg: db     27,'[>4v','$'
prefix: db      27,'[>4;','$'
one:    db      '1'
zero:   db      '0'

remaining: db    0
tokenlen:  db    0
zoomlen:   db    1
xlen:      db    1
ylen:      db    1
tokenptr:  dw    0
endptr:    dw    0
zoomptr:   dw    one
xptr:      dw    zero
yptr:      dw    zero
