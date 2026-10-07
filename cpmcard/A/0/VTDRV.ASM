; VTDRV.COM - sequential CP/M drive-path benchmark for RunCPM.
; Requires the 7xtgv RunCPM host timer on private I/O port 7Ah.
;
; Plain VTDRV is read-only: it cyclically reads 32 KiB from A0:MBASIC.COM,
; reports the rate, then verifies four more passes.  VTDRV RW (or /RW)
; explicitly enables the old temporary create/write/read/delete test.

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
        call    parse_mode
        or      a
        jp      z,readonly
        inc     a
        jp      z,bad_option

; Explicit RW mode starts here.
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

readonly:
        ld      de,ro_banner
        ld      c,print
        call    bdos
        ld      a,rbegin
        out     (port),a
        call    ro_pass
        jp      nz,ro_read_error
        ld      a,rend
        out     (port),a

; Four untimed checksummed passes catch unstable reads without distorting the
; reported BDOS/SD rate.  The first establishes the reference checksum.
        call    ro_verify
        jp      nz,ro_read_error
        ld      a,(sum1)
        ld      (base1),a
        ld      a,(sum2)
        ld      (base2),a
        ld      a,3
        ld      (passes),a
ro_verify_loop:
        call    ro_verify
        jp      nz,ro_read_error
        ld      a,(base1)
        ld      hl,sum1
        cp      (hl)
        jp      nz,ro_data_error
        ld      a,(base2)
        ld      hl,sum2
        cp      (hl)
        jp      nz,ro_data_error
        ld      hl,passes
        dec     (hl)
        jp      nz,ro_verify_loop
        ld      de,ro_passed
        ld      c,print
        call    bdos
        jp      0

bad_option:
        ld      de,usage_message
        jr      show_and_exit

ro_read_error:
        ld      a,cancel
        out     (port),a
        ld      de,ro_read_message
        jr      show_and_exit

ro_data_error:
        ld      de,ro_data_message
        jr      show_and_exit

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

; Return A=0 for no option, A=1 for explicit RW, A=FF for anything else.
parse_mode:
        ld      a,(0080h)
        or      a
        ret     z
        ld      hl,0081h
parse_skip:
        ld      a,(hl)
        cp      ' '
        jr      nz,parse_first
        inc     hl
        jr      parse_skip
parse_first:
        cp      '/'
        jr      nz,parse_r
        inc     hl
        ld      a,(hl)
parse_r:
        and     05fh
        cp      'R'
        jr      nz,parse_bad
        inc     hl
        ld      a,(hl)
        and     05fh
        cp      'W'
        jr      nz,parse_bad
        ld      a,1
        ret
parse_bad:
        ld      a,0ffh
        ret

prepare_ro_fcb:
        ld      hl,ro_fcb_template
        ld      de,ro_fcb
        ld      b,fcblen
ro_copy_fcb:
        ld      a,(hl)
        ld      (de),a
        inc     hl
        inc     de
        djnz    ro_copy_fcb
        ret

open_ro:
        call    prepare_ro_fcb
        ld      de,ro_fcb
        ld      c,open
        call    bdos
        cp      0ffh
        ret

close_ro:
        ld      de,ro_fcb
        ld      c,close
        jp      bdos

; Read one record, cycling to the start of MBASIC.COM at EOF.
ro_read_next:
        ld      de,ro_fcb
        ld      c,read
        call    bdos
        or      a
        ret     z
        cp      1
        ret     nz
        call    close_ro
        call    open_ro
        jr      z,ro_next_fail
        ld      de,ro_fcb
        ld      c,read
        call    bdos
        or      a
        ret
ro_next_fail:
        ld      a,0ffh
        or      a
        ret

; Read exactly 256 records (32 KiB), reopening at EOF as necessary.
ro_pass:
        call    open_ro
        jr      z,ro_pass_fail
        call    set_dma
        ld      hl,records
        ld      (count),hl
ro_pass_loop:
        call    ro_read_next
        or      a
        jr      nz,ro_pass_fail_close
        call    count_down
        jr      nz,ro_pass_loop
        call    close_ro
        xor     a
        ret
ro_pass_fail_close:
        call    close_ro
ro_pass_fail:
        ld      a,1
        or      a
        ret

; Fletcher-like two-byte checksum for a complete 32 KiB read pass.
ro_verify:
        call    open_ro
        jr      z,ro_verify_fail
        call    set_dma
        xor     a
        ld      (sum1),a
        ld      (sum2),a
        ld      hl,records
        ld      (count),hl
ro_verify_record:
        call    ro_read_next
        or      a
        jr      nz,ro_verify_fail_close
        ld      hl,buffer
        ld      b,128
ro_sum_byte:
        ld      a,(sum1)
        add     a,(hl)
        ld      (sum1),a
        ld      c,a
        ld      a,(sum2)
        add     a,c
        ld      (sum2),a
        inc     hl
        djnz    ro_sum_byte
        call    count_down
        jr      nz,ro_verify_record
        call    close_ro
        xor     a
        ret
ro_verify_fail_close:
        call    close_ro
ro_verify_fail:
        ld      a,1
        or      a
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
        db      'VTDRV: default RO; use VTDRV RW to write-test',13,10,'$'
ro_banner:
        db      'VTDRV RO: 32 KiB cyclic read of A0:MBASIC.COM',13,10,'$'
ro_passed:
        db      'VTDRV RO verify: PASS (4 checksum passes)',13,10,'$'
usage_message:
        db      'Usage: VTDRV [RW]',13,10,'$'
ro_read_message:
        db      'VTDRV RO read/open failed.',13,10,'$'
ro_data_message:
        db      'VTDRV RO verify mismatch.',13,10,'$'
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
ro_fcb_template:
        db      1,'MBASIC  ','COM'
        ds      24,0
fcb:    ds      fcblen,0
ro_fcb: ds      fcblen,0
count:  dw      0
passes: db      0
sum1:   db      0
sum2:   db      0
base1:  db      0
base2:  db      0
buffer: ds      128,0
                                                                          