; VTSTOP.COM -- print elapsed milliseconds since VTSTART and remove VTIME.DAT.
; The FCB drive is zero, so it reads the current CP/M drive/user stamp.

        org     0100h

bdos    equ     5
read    equ     20
delete  equ     19
setdma  equ     26
close   equ     16
open    equ     15
uptime  equ     248             ; RunCPM: DE:HL = Arduino millis()
dma     equ     0080h

start:  lxi     d,fcb
        mvi     c,open
        call    bdos
        ora     a
        jnz     missing
        lxi     d,dma
        mvi     c,setdma
        call    bdos
        lxi     d,fcb
        mvi     c,read
        call    bdos
        ora     a
        jnz     badstamp
        lxi     d,fcb
        mvi     c,close
        call    bdos

        mvi     c,uptime
        call    bdos
        shld    now
        xchg
        shld    now+2

; elapsed = now - saved timestamp, modulo 2^32 (safe across millis wrap).
        lhld    now
        xchg
        lhld    dma
        mov     a,e
        sub     l
        sta     elapsed
        mov     a,d
        sbb     h
        sta     elapsed+1
        lhld    now+2
        xchg
        lhld    dma+2
        mov     a,e
        sbb     l
        sta     elapsed+2
        mov     a,d
        sbb     h
        sta     elapsed+3

        lxi     d,prefix
        mvi     c,9
        call    bdos
        call    printu32
        lxi     d,suffix
        mvi     c,9
        call    bdos
        lxi     d,fcb
        mvi     c,delete
        call    bdos
        ret

missing:lxi     d,nomsg
        mvi     c,9
        call    bdos
        ret
badstamp:
        lxi     d,badmsg
        mvi     c,9
        call    bdos
        ret

; Print unsigned 32-bit elapsed as decimal. For each decimal power, repeatedly
; subtract its 32-bit divisor (at most nine times) and emit the digit.
; CP/M ASM 2.0 does not resolve an underscore-containing forward label here.
; Keep this symbol alphanumeric so the generated CALL targets this routine.
printu32:
        lxi     h,divtab
        mvi     a,10
        sta     remaining
        xra     a
        sta     printed
nextdiv:mov     a,m
        sta     divisor
        inx     h
        mov     a,m
        sta     divisor+1
        inx     h
        mov     a,m
        sta     divisor+2
        inx     h
        mov     a,m
        sta     divisor+3
        inx     h
        mvi     c,0
subloop:call    cmpdiv
        jc      emit
        call    subdiv
        inr     c
        jmp     subloop
emit:   lda     remaining
        cpi     1
        jz      digit
        lda     printed
        ora     a
        jnz     digit
        mov     a,c
        ora     a
        jz      advance
digit:  mov     a,c
        adi     '0'
        mov     e,a
        push    h
        mvi     c,2
        call    bdos
        pop     h
        mvi     a,1
        sta     printed
advance:lda     remaining
        dcr     a
        sta     remaining
        jnz     nextdiv
        ret

; Return carry when elapsed is smaller than divisor; preserve HL table pointer.
cmpdiv:lda     elapsed+3
        mov     d,a
        lda     divisor+3
        mov     e,a
        mov     a,d
        cmp     e
        jc      cmpret
        jnz     cmpge
        lda     elapsed+2
        mov     d,a
        lda     divisor+2
        mov     e,a
        mov     a,d
        cmp     e
        jc      cmpret
        jnz     cmpge
        lda     elapsed+1
        mov     d,a
        lda     divisor+1
        mov     e,a
        mov     a,d
        cmp     e
        jc      cmpret
        jnz     cmpge
        lda     elapsed
        mov     d,a
        lda     divisor
        mov     e,a
        mov     a,d
        cmp     e
cmpret:ret
cmpge:  ora     a               ; clear carry
        ret

subdiv: lda     elapsed
        mov     d,a
        lda     divisor
        mov     e,a
        mov     a,d
        sub     e
        sta     elapsed
        lda     elapsed+1
        mov     d,a
        lda     divisor+1
        mov     e,a
        mov     a,d
        sbb     e
        sta     elapsed+1
        lda     elapsed+2
        mov     d,a
        lda     divisor+2
        mov     e,a
        mov     a,d
        sbb     e
        sta     elapsed+2
        lda     elapsed+3
        mov     d,a
        lda     divisor+3
        mov     e,a
        mov     a,d
        sbb     e
        sta     elapsed+3
        ret

fcb:    db      0,'VTIME   ','DAT'
        db      0,0,0,0,0,0,0,0,0,0,0,0
        db      0,0,0,0,0,0,0,0,0,0,0,0
now:    db      0,0,0,0
elapsed:db      0,0,0,0
divisor:db      0,0,0,0
remaining: db   0
printed: db     0
divtab: db      0,202,154,59       ; 1000000000
        db      0,225,245,5        ; 100000000
        db      128,150,152,0      ; 10000000
        db      64,66,15,0         ; 1000000
        db      160,134,1,0        ; 100000
        db      16,39,0,0          ; 10000
        db      232,3,0,0          ; 1000
        db      100,0,0,0          ; 100
        db      10,0,0,0           ; 10
        db      1,0,0,0            ; 1
prefix: db      'VTSTOP: ', '$'
suffix: db      ' ms',13,10,'$'
nomsg:  db      'VTSTOP: no VTIME.DAT on this drive/user$'
badmsg: db      'VTSTOP: cannot read VTIME.DAT$'
        end     start
