; VTZOOM -- inspect or temporarily select 7xtgv view/terminal geometry.
; VTZOOM [n [COLSxROWS] | DEFAULT], n=1..4.
; Build: ASM VTZOOM, then LOAD VTZOOM (Intel 8080 / DRI ASM).
        org     0100h
        mvi     c,11
        call    5
        ora     a
        jnz     busy
        lda     0080h
        mov     b,a
        lxi     h,0081h
        call    spaces
        mov     a,b
        ora     a
        jz      show
        mov     a,m
        cpi     'd'
        jz      default
        cpi     'D'
        jz      default
        cpi     '1'
        jc      usage
        cpi     '5'
        jnc     usage
        sta     choice
        inx     h
        dcr     b
        call    spaces
        mov     a,b
        ora     a
        jz      preset
        lxi     d,command+7
        mvi     a,';'
        stax    d
        inx     d
        call    digits
        mov     a,c
        ora     a
        jz      usage
        mov     a,b
        ora     a
        jz      usage
        mov     a,m
        cpi     'X'
        jz      separator
        cpi     'x'
        jnz     usage
separator:
        mvi     a,';'
        stax    d
        inx     d
        inx     h
        dcr     b
        call    digits
        mov     a,c
        ora     a
        jz      usage
        call    spaces
        mov     a,b
        ora     a
        jnz     usage
        mvi     a,'v'
        stax    d
        inx     d
        mvi     a,'$'
        stax    d
        lxi     d,command
        call    print
        jmp     show
preset: lxi     d,command+7
        mvi     a,'v'
        stax    d
        inx     d
        mvi     a,'$'
        stax    d
        lxi     d,command
        call    print
        jmp     show
default:
        lxi     d,word
dloop:  ldax    d
        ora     a
        jz      dtail
        mov     c,a
        mov     a,b
        ora     a
        jz      usage
        mov     a,m
        cpi     'a'
        jc      dcmp
        cpi     '{'
        jnc     dcmp
        sui     32
dcmp:   cmp     c
        jnz     usage
        inx     d
        inx     h
        dcr     b
        jmp     dloop
dtail:  call    spaces
        mov     a,b
        ora     a
        jnz     usage
        lxi     d,resetcmd
        call    print
show:   lxi     d,query
        call    print
        call    reply
        cpi     1
        jz      absent
        ora     a
        jnz     corrupt
        call    parse
        ora     a
        jnz     corrupt
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
        lxi     d,crlf
        jmp     print
usage:  lxi     d,help
        jmp     print
absent: lxi     d,nosupport
        jmp     print
corrupt:
        lxi     d,badreply
        jmp     print
busy:   lxi     d,pending
        jmp     print

; Skip trailing or inter-token spaces, with B remaining and HL source.
spaces: mov     a,b
        ora     a
        rz
        mov     a,m
        cpi     ' '
        rnz
        inx     h
        dcr     b
        jmp     spaces
; Copy 1..3 decimal digits from HL to DE, leaving C=count.
digits: mvi     c,0
digit:  mov     a,b
        ora     a
        rz
        mov     a,m
        cpi     '0'
        rc
        cpi     ':'
        rnc
        mov     a,c
        cpi     3
        rz
        mov     a,m
        stax    d
        inx     d
        inx     h
        dcr     b
        inr     c
        jmp     digit
print:  mvi     c,9
        call    5
        ret
field:  add     a
        mov     e,a
        mvi     d,0
        lxi     h,fields
        dad     d
        mov     e,m
        inx     h
        mov     d,m
        jmp     print

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

word:   db      'DEFAULT',0
command:
        db      27,'[>73;'
choice: db      '1',';000;000v$'
resetcmd:
        db      27,'[>73;0v$'
query:  db      27,'[>72v$'
magic:  db      '7xtgv-info;1;',0
view:   db      'View    : zoom $'
logical:db      ', logical $'
visible:db      ', visible $'
cross:  db      'x$'
crlf:   db      13,10,'$'
help:   db      'VTZOOM [1..4 [COLSxROWS] | DEFAULT]',13,10,'$'
nosupport:
        db      'VTZOOM: NOT SUPPORTED (timeout)',13,10,'$'
badreply:
        db      'VTZOOM: BAD REPLY',13,10,'$'
pending:
        db      'VTZOOM: INPUT PENDING',13,10,'$'
fields: ds      42
body:   ds      230
        end
