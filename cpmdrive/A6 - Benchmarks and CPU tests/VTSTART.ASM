; VTSTART.COM -- timestamp the current RunCPM uptime in VTIME.DAT.
; The FCB drive is zero, so the file belongs to the current CP/M drive/user.

        org     0100h

bdos    equ     5
delete  equ     19
write   equ     21
make    equ     22
setdma  equ     26
close   equ     16
uptime  equ     248             ; RunCPM: DE:HL = Arduino millis()
dma     equ     0080h

start:  mvi     c,uptime
        call    bdos
        shld    dma
        xchg
        shld    dma+2

        lxi     d,fcb
        mvi     c,delete         ; replace an earlier stamp if present
        call    bdos
        lxi     d,fcb
        mvi     c,make
        call    bdos
        ora     a
        jnz     error

        lxi     d,dma
        mvi     c,setdma
        call    bdos
        lxi     d,fcb
        mvi     c,write
        call    bdos
        ora     a
        jnz     error
        lxi     d,fcb
        mvi     c,close
        call    bdos

        lxi     d,okmsg
        mvi     c,9
        call    bdos
        ret

error:  lxi     d,errmsg
        mvi     c,9
        call    bdos
        ret

fcb:    db      0,'VTIME   ','DAT'
        db      0,0,0,0,0,0,0,0,0,0,0,0
        db      0,0,0,0,0,0,0,0,0,0,0,0
okmsg:  db      'VTSTART: stamp saved$'
errmsg: db      'VTSTART: cannot write VTIME.DAT$'
        end     start
