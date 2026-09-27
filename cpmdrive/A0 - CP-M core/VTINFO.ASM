; VTINFO -- 7xtgv display and viewport report.
; Build: ASM VTINFO, then LOAD VTINFO (Intel 8080 / DRI ASM).
        org     0100h
        mvi     c,11
        call    5
        ora     a
        jnz     busy
        lxi     d,query
        call    print
        call    reply
        cpi     1
        jz      absent
        ora     a
        jnz     corrupt
        call    parse
        ora     a
        jnz     corrupt
        lxi     d,display
        call    print
        mvi     a,0
        call    field
        lxi     d,target
        call    print
        mvi     a,1
        call    field
        lxi     d,panel
        call    print
        mvi     a,3
        call    field
        lxi     d,cross
        call    print
        mvi     a,4
        call    field
        lxi     d,build
        call    print
        mvi     a,2
        call    field
        lxi     d,view
        call    print
        mvi     a,5
        call    field
        lxi     d,logical
        call    print
        mvi     a,6
        call    field
        lxi     d,cross
        call    print
        mvi     a,7
        call    field
        lxi     d,visible
        call    print
        mvi     a,8
        call    field
        lxi     d,cross
        call    print
        mvi     a,9
        call    field
        lxi     d,blanks
        call    print
        lxi     d,presets
        call    print
        mvi     a,13
        sta     index
        mvi     a,'1'
        sta     scale
rows:   lxi     d,scale
        call    print
        lxi     d,xlabel
        call    print
        lda     index
        call    field
        lxi     d,cross
        call    print
        lda     index
        inr     a
        call    field
        lxi     d,crlf
        call    print
        lda     index
        adi     2
        sta     index
        lda     scale
        inr     a
        sta     scale
        cpi     '5'
        jnz     rows
        ret
busy:   lxi     d,pending
        jmp     print
absent: lxi     d,nosupport
        jmp     print
corrupt:
        lxi     d,badreply
        jmp     print

; BDOS 9 prints a dollar-terminated string at DE.
print:  mvi     c,9
        call    5
        ret
; Field number in A. The parser replaces semicolons with dollar markers.
field:  add     a
        mov     e,a
        mvi     d,0
        lxi     h,fields
        dad     d
        mov     e,m
        inx     h
        mov     d,m
        jmp     print

; Poll at most 65535 times per byte. A=0 signals timeout.
readch: lxi     b,0ffffh
again: push    b
        mvi     e,0ffh
        mvi     c,6
        call    5
        pop     b
        ora     a
        rnz
        dcx     b
        mov     a,b
        ora     c
        jnz     again
        xra     a
        ret

; Consume only a bounded ESC P ... ESC backslash record.
reply:  call    readch
        ora     a
        jz      timeout
        cpi     27
        jnz     invalid
        call    readch
        ora     a
        jz      timeout
        cpi     'P'
        jnz     invalid
        lxi     h,body
        mvi     b,0
more:   push    b
        push    h
        call    readch
        pop     h
        pop     b
        ora     a
        jz      timeout
        cpi     27
        jz      endesc
        cpi     32
        jc      invalid
        cpi     127
        jnc     invalid
        cpi     '$'
        jz      invalid
        mov     m,a
        inx     h
        inr     b
        mov     a,b
        cpi     230
        jnc     invalid
        jmp     more
endesc: push    b
        push    h
        call    readch
        pop     h
        pop     b
        cpi     92
        jnz     invalid
        mvi     m,'$'
        xra     a
        ret
timeout:
        mvi     a,1
        ret
invalid:
        mvi     a,2
        ret

; Exact v1 prefix, then exactly 21 nonempty semicolon-separated fields.
parse:  lxi     h,body
        lxi     d,magic
prefix: ldax    d
        ora     a
        jz      first
        cmp     m
        jnz     malformed
        inx     d
        inx     h
        jmp     prefix
first:  lxi     d,fields
        mvi     b,0
entry:  mov     a,m
        cpi     '$'
        jz      malformed
        cpi     ';'
        jz      malformed
        mov     a,l
        stax    d
        inx     d
        mov     a,h
        stax    d
        inx     d
        inr     b
scan:   mov     a,m
        cpi     '$'
        jz      final
        cpi     ';'
        jz      split
        inx     h
        jmp     scan
split:  mov     a,b
        cpi     21
        jz      malformed
        mvi     m,'$'
        inx     h
        jmp     entry
final:  mov     a,b
        cpi     21
        jnz     malformed
        xra     a
        ret
malformed:
        mvi     a,2
        ret

query:  db      27,'[>72v','$'
magic:  db      '7xtgv-info;1;',0
display:db      'Display : $'
target: db      13,10,'Target  : $'
panel:  db      13,10,'Panel   : $'
build:  db      13,10,'Build   : $'
view:   db      13,10,'View    : zoom $'
logical:db      ', logical $'
visible:db      ', visible $'
cross:  db      'x$'
crlf:   db      13,10,'$'
blanks: db      13,10,13,10,'$'
presets:db      'Resolved presets:',13,10,'$'
scale:  db      '1','$'
xlabel: db      'x: $'
pending:db      'VTINFO: INPUT PENDING',13,10,'$'
nosupport:
        db      'VTINFO: NOT SUPPORTED (timeout)',13,10,'$'
badreply:
        db      'VTINFO: BAD REPLY',13,10,'$'
index:  db      0
fields: ds      42
body:   ds      230
        end
