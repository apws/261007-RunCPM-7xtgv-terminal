; VTDRV.COM - sequential CP/M drive-path benchmark for RunCPM.
; Requires the 7xtgv RunCPM host timer on private I/O port 7Ah.
;
; Creates VTDRV.$$$ on the current CP/M drive/user, writes and reads 256
; 128-byte records (32 KiB), reports both rates, verifies a second read pass,
; then deletes the temporary file.  Timed passes include CP/M BDOS calls and
; RunCPM's complete host filesystem/SD path.  Console output is outside them.

        org     0100h

port:   equ     07ah
bdos:   equ     0005h

print:  equ     9
open:   equ     15
close:  equ     16
delete: equ     19
read:   equ     20
write:  equ     21
make:   equ     22
setdma: equ     26

wbegin: equ     1
wend:   equ     2
rbegin: equ     3
rend:   equ     4
cancel: equ     255

records: equ    256
fcblen: equ     36

        in      a,(port)
        cp      'D'
        jp      nz,unsupported

        ld      de,banner
        ld      c,print
        call    bdos
        call    fill_buffer

; Remove a stale file from an interrupted earlier run.
        call    prepare_fcb
        ld      de,fcb
        ld      c,delete
        call    bdos

; Timed create/write/close pass.
        ld      a,wbegin
        out     (port),a
        call    prepare_fcb
        ld      de,fcb
        ld      c,make
        call    bdos
        cp      0ffh
        jp      z,write_error
        call    set_dma
        ld      hl,records
        ld      (count),hl
write_loop:
        ld      de,fcb
        ld      c,write
        call    bdos
        or      a
        jp      nz,write_error
        call    count_down
        jr      nz,write_loop
        ld      de,fcb
        ld      c,close
        call    bdos
        cp      0ffh
        jp      z,write_error
        ld      a,wend
        out     (port),a

; Timed open/read/close pass.  Verification is deliberately separate so its
; interpreted byte-comparison loop does not distort the drive rate.
        ld      a,rbegin
        out     (port),a
        call    open_for_read
        jp      z,read_error
        ld      hl,records
        ld      (count),hl
read_loop:
        ld      de,fcb
        ld      c,read
        call    bdos
        or      a
        jp      nz,read_error
        call    count_down
        jr      nz,read_loop
        ld      de,fcb
        ld      c,close
        call    bdos
        cp      0ffh
        jp      z,read_error
        ld      a,rend
        out     (port),a

; Untimed full verification pass.
        call    open_for_read
        jr      z,verify_read_error
        ld      hl,records
        ld      (count),hl
verify_record:
        ld      de,fcb
        ld      c,read
        call    bdos
        or      a
        jr      nz,verify_read_error
        call    check_buffer
        jr      nz,verify_data_error
        call    count_down
        jr      nz,verify_record
        ld      de,fcb
        ld      c,read
        call    bdos
        cp      1
        jr      nz,verify_eof_error
        ld      de,fcb
        ld      c,close
        call    bdos

        call    remove_file
        ld      de,passed
        ld      c,print
        call    bdos
        jp      0

unsupported:
        ld      de,unsupported_message
        jr      show_and_exit

write_error:
        ld      a,cancel
        out     (port),a
        ld      de,write_message
        jr      failed

read_error:
        ld      a,cancel
        out     (port),a
        ld      de,read_message
        jr      failed

verify_read_error:
        ld      de,verify_read_message
        jr      failed
verify_data_error:
        ld      de,verify_data_message
        jr      failed
verify_eof_error:
        ld      de,verify_eof_message
failed:
        push    de
        call    remove_file
        pop     de
show_and_exit:
        ld      c,print
        call    bdos
        jp      0

prepare_fcb:
        ld      hl,fcb_template
        ld      de,fcb
        ld      b,fcblen
copy_fcb:
        ld      a,(hl)
        ld      (de),a
        inc     hl
        inc     de
        djnz    copy_fcb
        ret

open_for_read:
        call    prepare_fcb
        ld      de,fcb
        ld      c,open
        call    bdos
        cp      0ffh
        ret

set_dma:
        ld      de,buffer
        ld      c,setdma
        jp      bdos

count_down:
        ld      hl,(count)
        dec     hl
        ld      (count),hl
        ld      a,h
        or      l
        ret

fill_buffer:
        ld      hl,buffer
        ld      b,128
        xor     a
fill_byte:
        ld      (hl),a
        inc     hl
        inc     a
        djnz    fill_byte
        ret

check_buffer:
        ld      hl,buffer
        ld      b,128
        xor     a
check_byte:
        cp      (hl)
        ret     nz
        inc     hl
        inc     a
        djnz    check_byte
        xor     a                   ; successful full record: return Z
        ret

remove_file:
        call    prepare_fcb
        ld      de,fcb
        ld      c,delete
        jp      bdos

banner:
        db      'VTDRV: 32 KiB sequential BDOS test',13,10,'$'
passed:
        db      'VTDRV verify: PASS',13,10,'$'
unsupported_message:
        db      'VTDRV needs newer 7xtgv RunCPM firmware.',13,10,'$'
write_message:
        db      'VTDRV write failed.',13,10,'$'
read_message:
        db      'VTDRV read failed.',13,10,'$'
verify_read_message:
        db      'VTDRV verify read failed.',13,10,'$'
verify_data_message:
        db      'VTDRV verify data failed.',13,10,'$'
verify_eof_message:
        db      'VTDRV verify EOF failed.',13,10,'$'

fcb_template:
        db      0,'VTDRV   ','$$$'
        ds      24,0
fcb:    ds      fcblen,0
count:  dw      0
buffer: ds      128,0
