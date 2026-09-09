; VTCPU.COM - measure RunCPM's effective Z80 interpreter speed.
; Requires the 7xtgv RunCPM host service on private I/O port 7Bh.
;
; The first pass runs with the terminal active.  The second asks the host to
; pause terminal parsing, TFT rendering and USB mirroring during the loop.
; Both execute the same 13,015,509-T-state workload used by cpu_mhz.h.

        org     0100h

port:   equ     07bh
bdos:   equ     0005h
print:  equ     9

        in      a,(port)
        cp      'V'
        jr      nz,unsupported

        ld      a,1
        out     (port),a
        call    benchmark
        ld      a,3
        out     (port),a

        ld      a,2
        out     (port),a
        call    benchmark
        ld      a,3
        out     (port),a
        jp      0

unsupported:
        ld      de,message
        ld      c,print
        call    bdos
        jp      0

benchmark:
        ld      de,500
outer:
        ld      bc,1000
inner:
        dec     bc
        ld      a,b
        or      c
        jr      nz,inner
        dec     de
        ld      a,d
        or      e
        jr      nz,outer
        ret

message:
        db      'VTCPU needs newer 7xtgv RunCPM firmware.',13,10,'$'
