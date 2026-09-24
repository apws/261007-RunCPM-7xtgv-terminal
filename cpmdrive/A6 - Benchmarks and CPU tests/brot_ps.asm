;
;  Compute a Mandelbrot set on a simple Z80 computer.
;
; From https://rosettacode.org/wiki/Mandelbrot_set#Z80_Assembly
; Adapted to CP/M and colorzied by J.B. Langston
;
; Assemble with z88dk for RC2014 CP/M
; zcc +rc2014 -subtype=cpm -clib=sdcc_iy -v -m --list mandel.asm -o mandel
; appmake +glue --ihex --clean -b mandel -c mandel
;
; Assemble with z88dk for YAZ180 CP/M, for example:
; zcc +yaz180 -subtype=cpm -v -m --list mandel.asm -o mand180
; appmake +glue --ihex --clean -b mand180 -c mand180
;
; Assemble with z88dk for yabios app model, for example:
; zcc +yaz180 -subtype=app -v -m --list mandel.asm -o mandapu
; appmake +glue --ihex --clean -b mandapu -c mandapu
;
; To calculate the theoretical minimum time at 115200 baud.
; Normally 10 colour codes, and 1 character per point.
; A line is (3 x 80) x 11 + CR + LF = 2642 characters
; There are 10 x 60 / 4 lines = 150 lines
; Therefore 396,300 characters need to be transmitted.
; Serial rate is 115200 baud or 14,400 8 bit characters per second
;
; Therefore the theoretical minimum time is 27.52 seconds.
;
; Results          FCPU         Original    Optimised   z180 mlt
; RC2014 CP/M    7.432MHz         4'58"       3'48"
; YAZ180 CP/M   18.432MHz         1'40"       1'24"       1'00"
; YAZ180 yabios 18.432Mhz                     1'14"         54"
; YAZ180 CP/M   36.864MHz         1'06"         58"         46"
; YAZ180 yabios 36.864Mhz                       56"         45"
; 
;
; Porting this program to another Z80 platform should be easy and straight-
; forward: The only dependencies on my homebrew machine are the system-calls
; used to print strings and characters. These calls are performed by loading
; IX with the number of the system-call and performing an RST 08. To port this
; program to another operating system just replace these system-calls with
; the appropriate versions. Only three system-calls are used in the following:
; _crlf: Prints a CR/LF, _puts: Prints a 0-terminated string (the adress of
; which is expected in HL), and _putc: Print a single character which is
; expected in A. RST 0 give control back to the monitor.
;

; include "config_yaz180_private.inc"

defc _CPM       = 1
defc _APU       = 0
defc _DOUBLE    = 0

IF !_CPM
extern asm_pchar, asm_pstring
ENDIF

IF _APU
extern asm_am9511a_reset, asm_am9511a_opp, asm_am9511a_cmd
extern asm_am9511a_isr, asm_am9511a_chk_idle
ENDIF

defc bdos       = 05h                     ; bdos vector
defc conout     = 2                       ; console output bdos call
defc condio     = 6                       ; console direct I/O call
defc prints     = 9                       ; print string bdos call
defc cr         = 13                      ; carriage return
defc lf         = 10                      ; line feed
defc esc        = 27                      ; escape

IF _CPM                                   ; cp/m ram model 
defc eos        = '$'                     ; end of string marker
ELSE
defc eos        = 0                       ; end of string marker
ENDIF

defc pixel      = '#'                     ; character to output for pixel
defc SCALE      = 256                     ; Do NOT change this - the
                                          ; arithmetic routines rely on
                                          ; this scaling factor! :-)

;------------------------------------------------------------------------------

SECTION  data_user

x:              defw    0                       ; x-coordinate
x_start:        defw    -2 * SCALE              ; Minimum x-coordinate
x_end:          defw    1 * SCALE               ; Maximum x-coordinate
x_step:         defw    SCALE / 80              ; x-coordinate step-width

y:              defw    0                       ; y-coordinate
y_start:        defw    -5 * SCALE / 4          ; Minimum y-coordinate   
y_end:          defw    5 * SCALE / 4           ; Maximum y-coordinate
y_step:         defw    SCALE / 60              ; y-coordinate step-width

iteration_max:  defb    30                      ; How many iterations
divergent:      defw    SCALE * 4
scale:          defw    SCALE
                defw    0

z_0:            defs    4,0
z_1:            defs    4,0

z_2:            defw    0

z_0_square:
z_0_square_low: defw    0
z_0_square_high:defw    0
z_1_square:
z_1_square_low: defw    0
z_1_square_high:defw    0


display:        defm    " .-+*=#@"              ; 8 characters for the display

hsv:            defm 0                          ; hsv color table
                defm 201, 200, 199, 198, 197
                defm 196, 202, 208, 214, 220
                defm 226, 190, 154, 118,  82
                defm 46,   47,  48,  49,  50
                defm 51,   45,  39,  33,  27
                defm 21,   57,  93, 129, 165

welcome:        defm    "Generating a Mandelbrot set"
crlf:           defm    cr, lf, eos
finished:       defm    esc, "[0mComputation finished.", cr, lf, eos
ansifg:         defm    esc, "[38;5;", eos
ansibg:         defm    esc, "[48;5;", eos

;------------------------------------------------------------------------------

SECTION code_user

PUBLIC _main

._main
        ld      de, welcome             ; Print a welcome message
IF _CPM                                 ; cp/m ram model 
        call    prtmesg
ELSE
        call    asm_pstring
ENDIF
        call    delay
        call    delay

IF _APU

        call    delay
        call    delay
                                        ; set DMA/Wait I/O states
        ld      a,DCNTL_MWI1|DCNTL_IWI1
        out0    (DCNTL),a               ; 2 Memory Wait & 3 I/O Wait

        call    asm_am9511a_reset       ; INITIALISE THE APU
ENDIF

; for (y = <initial_value> ; y <= y_end; y += y_step)
; {
        ld      hl, (y_start)           ; y = y_start
        ld      (y), hl
.outer_loop
        ld      hl, (y_end)             ; Is y <= y_end?
        ld      de, (y)
        and     a                       ; Clear carry
        sbc     hl, de                  ; Perform the comparison
        jp      M, mandel_end           ; End of outer loop reached

;    for (x = x_start; x <= x_end; x += x_step)
;    {
        ld      hl, (x_start)           ; x = x_start
        ld      (x), hl
.inner_loop
        ld      hl, (x_end)             ; Is x <= x_end?
        ld      de, (x)
        and     a
        sbc     hl, de
        jp      M, inner_loop_end       ; End of inner loop reached

;       push af
;       push hl
;       ld hl,(x)
;       call asm_phexwd
;       ld l,':'
;       call asm_pchar
;       ld hl,(y)
;       call asm_phexwd
;       ld l,'>'
;       call asm_pchar
;       call delay
;       call delay
;       pop hl
;       pop af

;      z_0 = z_1 = 0;
        ld      hl, 0
        ld      (z_0), hl
        ld      (z_1), hl

;      for (iteration = iteration_max; iteration; iteration--)
;      {
        ld      a, (iteration_max)
        ld      b, a
.iteration_loop
        push    bc                      ; iteration -> stack
        
IF _APU

        call    apu_calc
        ld      hl,(z_2)                ; (z_0 ^ 2 - z_1 ^ 2) / scale

ELSE                                    ; IF !_APU

;        z2 = (z_0 * z_0 - z_1 * z_1) / scale;

        ld      hl, (z_1)               ; Compute DE HL = z_1 * z_1
        ld      d, h
        ld      e, l
        call    l_muls_32_16x16
        ld      (z_1_square_low), hl    ; z_1 ** 2 is needed later again
        ld      (z_1_square_high), de

        ld      hl, (z_0)               ; Compute DE HL = z_0 * z_0
        ld      d, h
        ld      e, l
        call    l_muls_32_16x16
        ld      (z_0_square_low), hl    ; z_1 ** 2 will be also needed
        ld      (z_0_square_high), de

        and     a                       ; Compute subtraction
        ld      bc, (z_1_square_low)
        sbc     hl, bc
        push    hl                      ; Save lower 16 bit of result
        ld      h, d
        ld      l, e
        ld      bc, (z_1_square_high)
        sbc     hl, bc
        pop     bc                      ; HL BC = z_0 ^ 2 - z_1 ^ 2

        ld      c, b                    ; Divide by scale = 256
        ld      b, l                    ; Discard the rest
        push    bc                      ; We need BC later

;        z_3 = 2 * z_0 * z_1 / scale;
        ld      hl, (z_0)               ; Compute DE HL = 2 * z_0 * z_1
        add     hl, hl
        ld      de, (z_1)
        call    l_muls_32_16x16

        ld      b, e                    ; Divide by scale (= 256)
        ld      c, h                    ; BC contains now z_3

;        z_1 = z_3 + y;
        ld      hl, (y)
        add     hl, bc
        ld      (z_1), hl

;        z_0 = z_2 + x;
        pop     bc                      ; Here BC is needed again :-)
        ld      hl, (x)
        add     hl, bc
        ld      (z_0), hl

;        if (z_0 * z_0 / scale + z_1 * z_1 / scale > 4 * scale)
        ld      hl, (z_0_square_low)    ; Use the squares computed
        ld      de, (z_1_square_low)    ; above
        add     hl, de
        ld      b, h                    ; BC contains lower word of sum
        ld      c, l

        ld      hl, (z_0_square_high)
        ld      de, (z_1_square_high)
        adc     hl, de

        ld      h, l                    ; HL now contains (z_0 ^ 2 -
        ld      l, b                    ; z_1 ^ 2) / scale
        
        ld      (z_2),hl                ; save z_2

ENDIF                                   ; IF _APU

        ld      bc,(divergent)
        and     a
        sbc     hl, bc

;          break;
        jr      C, iteration_dec        ; No break
        pop     bc                      ; Get latest iteration counter
        jr      iteration_end           ; Exit loop

;        iteration++;
.iteration_dec
        pop     bc                      ; Get iteration counter
        djnz    iteration_loop          ; We might fall through!
;      }
.iteration_end
;      printf("%c", display[iteration % 7]);
;       call    asciipixel              ; Print the character
        call    colorpixel              ; Print the character

;       ld l,' '
;       call asm_pchar
;       ld hl,(z_2)
;       call asm_phexwd                     ; print final z_2
;       ld l,' '
;       call asm_pchar
;       ld l,b
;       call asm_phex                       ; print the iteration
;       ld l,cr
;       call asm_pchar
;       ld l,lf
;       call asm_pchar
;       call delay
;       call delay
        
IF _CPM                                 ; cp/m ram model 
        ld      c, condio
        call    bdos
ELSE
        ld      l,e
        call    asm_pchar
ENDIF

IF _APU
        call    delay                   ; wait until colour sequence is finished
        call    delay
ENDIF

        ld      de, (x_step)            ; x += x_step
        ld      hl, (x)
        add     hl, de
        ld      (x), hl

        jp      inner_loop
;    }
;    printf("\n");
.inner_loop_end
IF _CPM                                 ; cp/m ram model 
        ld      de, crlf                ; Print a CR/LF pair
        call    prtmesg
ELSE
        ld      l,cr
        call    asm_pchar
        ld      l,lf
        call    asm_pchar
ENDIF
        ld      de, (y_step)            ; y += y_step
        ld      hl, (y)
        add     hl, de
        ld      (y), hl                 ; Store new y-value

        jp      outer_loop
; }

.mandel_end
        call    delay
        call    delay

IF _APU
                                        ; set DMA/Wait I/O states
        ld      a,DCNTL_MWI0|DCNTL_IWI1
        out0    (DCNTL),a               ; 1 Memory Wait & 3 I/O Wait
ENDIF

        ld      de, finished            ; Print finished-message
IF _CPM                                 ; cp/m ram model 
        call    prtmesg
ELSE
        call    asm_pstring
ENDIF
        ret                             ; Return to CP/M or yabios
                
.colorpixel
        ld      a,b                     ; iter count in B -> C
        and     $1F                     ; lower five bits only
        ld      c,a
        ld      b,0
        ld      hl, hsv                 ; get ANSI color code
        add     hl, bc
        ld      a,(hl)
        call    setcolor
        ld      e, pixel                ; show pixel
        ret
                
.asciipixel
        ld      a, b                    ; iter count in B -> L
        and     $07                     ; lower three bits only
        sbc     hl, hl
        ld      l, a
        ld      de, display             ; Get start of character array
        add     hl, de                  ; address and load the
        ld      e, (hl)                 ; character to be printed
        ret

.setcolor
        push    af                      ; save accumulator
        ld      de,ansifg               ; ANSI foreground sequence
IF _CPM                                 ; cp/m ram model 
        call    prtmesg
ELSE
        call    asm_pstring
ENDIF
        pop     af
        call    printdec                ; print ANSI color code
IF _CPM                                 ; cp/m ram model 
        ld      e,'m'                   ; finish control sequence
        ld      c, condio
        call    bdos
ELSE
        ld      l,'m'                   ; finish control sequence
        call    asm_pchar
ENDIF
        ret
                
.printdec
        ld      c,-100                  ; print 100s place
        call    pd1
        ld      c,-10                   ; 10s place
        call    pd1
        ld      c,-1                    ; 1s place
.pd1
        ld      e,'0'-1                 ; start ASCII right before 0
.pd2
        inc     e                       ; increment ASCII code
        add     a,c                     ; subtract 1 place value
        jr      C,pd2                   ; loop until negative
        sub     c                       ; add back the last value
        push    af                      ; save accumulator
        ld      a,-1                    ; are we in the ones place?
        cp      c
        jr      Z,pd3                   ; if so, skip to output
        ld      a,'0'                   ; don't print leading 0s
        cp      e
        jr      Z,pd4
.pd3
IF _CPM                                 ; cp/m ram model 
        ld      c, condio
        call    bdos
ELSE
        ld      l,e
        call    asm_pchar
ENDIF
.pd4
        pop     af                      ; restore accumulator
        ret

;   Print message pointed to by (DE). It will end with a '$'.
;   modifies AF, DE, & HL
.prtmesg
        ld      a,(de)      ; Get character from DE address
        cp      '$'
        ret     Z
        inc     de
        push    de        ;otherwise, bump pointer and print it.
        ld      e,a
        ld      c, condio
        call    bdos
        pop     de
        jr      prtmesg

IF _APU

.apu_calc

IF _DOUBLE

;;;;;;; double calc

        ld hl,z_0                       ; Extend 16 bit z_0 to 32 bit
        inc hl
        ld a,(hl)
        add a,a                         ; Put sign bit into carry
        sbc a,a                         ; A = 0 if carry == 0, $FF otherwise
        inc hl
        ld (hl),a
        inc hl
        ld (hl),a

        ld hl,z_1                       ; Extend 16 bit z_1 to 32 bit
        inc hl
        ld a,(hl)
        add a,a                         ; Put sign bit into carry
        sbc a,a                         ; A = 0 if carry == 0, $FF otherwise
        inc hl
        ld (hl),a
        inc hl
        ld (hl),a

;       z_2 = (z_0 * z_0 - z_1 * z_1) / scale;

        ld de,z_0
        ld bc,__IO_APU_OP_ENT32         ; ENTER 32 bit (double)
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld c,__IO_APU_OP_PTOD           ; COMMAND for PTOD (push double)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld c,__IO_APU_OP_DMUL           ; COMMAND for DMUL (multiply lower)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld c,__IO_APU_OP_PTOD           ; COMMAND for PTOD (push double)
        call asm_am9511a_cmd            ; ENTER a COMMAND
        
        ld de,z_0_square
        ld bc,__IO_APU_OP_REM32         ; REMOVE 32 bit (double) to z_0_square
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER
        
        ld de,z_1
        ld bc,__IO_APU_OP_ENT32         ; ENTER 32 bit (double)
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld c,__IO_APU_OP_PTOD           ; COMMAND for PTOD (push double)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld c,__IO_APU_OP_DMUL           ; COMMAND for DMUL (multiply lower)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld c,__IO_APU_OP_PTOD           ; COMMAND for PTOD (push double)
        call asm_am9511a_cmd            ; ENTER a COMMAND
        
        ld de,z_1_square
        ld bc,__IO_APU_OP_REM32         ; REMOVE 32 bit (double) to z_1_square
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld c,__IO_APU_OP_DSUB           ; COMMAND for DSUB (subtract double)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld de,scale
        ld bc,__IO_APU_OP_ENT32         ; ENTER 32 bit (double)
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld c,__IO_APU_OP_DDIV           ; COMMAND for DDIV (divide double)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld c,__IO_APU_OP_POPS           ; COMMAND for POPS (pop single)
        call asm_am9511a_cmd            ; ENTER a COMMAND   
        
;       z_0 = z_2 + x;

        ld de,x
        ld bc,__IO_APU_OP_ENT16         ; ENTER 16 bit (word)
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld c,__IO_APU_OP_SADD           ; COMMAND for SADD (add single)
        call asm_am9511a_cmd            ; ENTER a COMMAND

;       z_3 = 2 * z_0 * z_1 / scale;

        ld de,z_0
        ld bc,__IO_APU_OP_ENT32         ; ENTER 32 bit (double)
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld c,__IO_APU_OP_PTOD           ; COMMAND for PTOD (push double)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld c,__IO_APU_OP_DADD           ; COMMAND for DADD (add double)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld de,z_1
        ld bc,__IO_APU_OP_ENT32         ; ENTER 32 bit (double)
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld c,__IO_APU_OP_DMUL           ; COMMAND for DMUL (multiply lower)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld de,scale
        ld bc,__IO_APU_OP_ENT32         ; ENTER 32 bit (double)
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld c,__IO_APU_OP_DDIV           ; COMMAND for DDIV (divide double)
        call asm_am9511a_cmd            ; ENTER a COMMAND

;       z_1 = z_3 + y;

        ld c,__IO_APU_OP_POPS           ; COMMAND for POPS (pop single)
        call asm_am9511a_cmd            ; ENTER a COMMAND 

        ld de,y
        ld bc,__IO_APU_OP_ENT16         ; ENTER 16 bit (word)
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld c,__IO_APU_OP_SADD           ; COMMAND for SADD (add single)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld de,z_1
        ld bc,__IO_APU_OP_REM16         ; REMOVE 16 bit (word) to z_1
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER
        
        ld de,z_0
        ld bc,__IO_APU_OP_REM16         ; REMOVE 16 bit (word) to z_0
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

;       if (z_0 * z_0 / scale + z_1 * z_1 / scale > 4 * scale)

        ld de,z_0_square
        ld bc,__IO_APU_OP_ENT32         ; ENTER 32 bit (double) from z_0_square
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld de,z_1_square
        ld bc,__IO_APU_OP_ENT32         ; ENTER 32 bit (double) from z_1_square
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld c,__IO_APU_OP_DADD           ; COMMAND for DADD (add double)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld de,scale
        ld bc,__IO_APU_OP_ENT32         ; ENTER 32 bit (double)
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld c,__IO_APU_OP_DDIV           ; COMMAND for DDIV (divide double)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld c,__IO_APU_OP_POPS           ; COMMAND for POPS (pop single)
        call asm_am9511a_cmd            ; ENTER a COMMAND 

        ld de,z_2
        ld bc,__IO_APU_OP_REM16         ; REMOVE 16 bit (single) to z_2
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

ELSE                                    ; IF !_DOUBLE

;;;;;;; float calc

;       z_2 = (z_0 * z_0 - z_1 * z_1) / scale;

        ld de,z_0
        ld bc,__IO_APU_OP_ENT16         ; ENTER 16 bit (single)
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld c,__IO_APU_OP_FLTS           ; COMMAND for FLTS (float single)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld c,__IO_APU_OP_PTOF           ; COMMAND for PTOF (push float)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld c,__IO_APU_OP_FMUL           ; COMMAND for FMUL (multiply float)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld c,__IO_APU_OP_PTOF           ; COMMAND for PTOF (push float)
        call asm_am9511a_cmd            ; ENTER a COMMAND
        
        ld de,z_0_square
        ld bc,__IO_APU_OP_REM32         ; REMOVE 32 bit (float) to z_0_square
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER
        
        ld de,z_1
        ld bc,__IO_APU_OP_ENT16         ; ENTER 16 bit (single)
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld c,__IO_APU_OP_FLTS           ; COMMAND for FLTS (float single)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld c,__IO_APU_OP_PTOF           ; COMMAND for PTOF (push float)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld c,__IO_APU_OP_FMUL           ; COMMAND for FMUL (multiply float)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld c,__IO_APU_OP_PTOF           ; COMMAND for PTOF (push float)
        call asm_am9511a_cmd            ; ENTER a COMMAND
        
        ld de,z_1_square
        ld bc,__IO_APU_OP_REM32         ; REMOVE 32 bit (float) to z_1_square
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld c,__IO_APU_OP_FSUB           ; COMMAND for FSUB (subtract float)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld de,scale
        ld bc,__IO_APU_OP_ENT16         ; ENTER 16 bit (single)
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld c,__IO_APU_OP_FLTS           ; COMMAND for FLTS (float single)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld c,__IO_APU_OP_FDIV           ; COMMAND for FDIV (divide float)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld c,__IO_APU_OP_FIXS           ; COMMAND for FIXS (fix single)
        call asm_am9511a_cmd            ; ENTER a COMMAND        
        
;       z_0 = z_2 + x;

        ld de,x
        ld bc,__IO_APU_OP_ENT16         ; ENTER 16 bit (word)
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld c,__IO_APU_OP_SADD           ; COMMAND for SADD (add single)
        call asm_am9511a_cmd            ; ENTER a COMMAND

;       z_3 = 2 * z_0 * z_1 / scale;

        ld de,z_0
        ld bc,__IO_APU_OP_ENT16         ; ENTER 16 bit (single)
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld c,__IO_APU_OP_FLTS           ; COMMAND for FLTS (float single)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld c,__IO_APU_OP_PTOF           ; COMMAND for PTOF (push float)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld c,__IO_APU_OP_FADD           ; COMMAND for FADD (add float)
        call asm_am9511a_cmd            ; ENTER a COMMAND


        ld de,z_1
        ld bc,__IO_APU_OP_ENT16         ; ENTER 16 bit (single)
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld c,__IO_APU_OP_FLTS           ; COMMAND for FLTS (float single)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld c,__IO_APU_OP_FMUL           ; COMMAND for FMUL (multiply float)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld de,scale
        ld bc,__IO_APU_OP_ENT16         ; ENTER 16 bit (single)
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld c,__IO_APU_OP_FLTS           ; COMMAND for FLTS (float single)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld c,__IO_APU_OP_FDIV           ; COMMAND for FDIV (divide float)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld c,__IO_APU_OP_FIXS           ; COMMAND for FIXS (fix single)
        call asm_am9511a_cmd            ; ENTER a COMMAND

;       z_1 = z_3 + y;

        ld de,y
        ld bc,__IO_APU_OP_ENT16         ; ENTER 16 bit (word)
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld c,__IO_APU_OP_SADD           ; COMMAND for SADD (add single)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld de,z_1
        ld bc,__IO_APU_OP_REM16         ; REMOVE 16 bit (word) to z_1
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER
        
        ld de,z_0
        ld bc,__IO_APU_OP_REM16         ; REMOVE 16 bit (word) to z_0
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

;       if (z_0 * z_0 / scale + z_1 * z_1 / scale > 4 * scale)

        ld de,z_0_square
        ld bc,__IO_APU_OP_ENT32         ; ENTER 32 bit (float) from z_0_square
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld de,z_1_square
        ld bc,__IO_APU_OP_ENT32         ; ENTER 32 bit (float) from z_1_square
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld c,__IO_APU_OP_FADD           ; COMMAND for FADD (add float)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld de,scale
        ld bc,__IO_APU_OP_ENT16         ; ENTER 16 bit (word)
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

        ld c,__IO_APU_OP_FLTS           ; COMMAND for FLTS (float single)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld c,__IO_APU_OP_FDIV           ; COMMAND for FDIV (divide float)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld c,__IO_APU_OP_FIXS           ; COMMAND for FIXS (fix single)
        call asm_am9511a_cmd            ; ENTER a COMMAND

        ld de,z_2
        ld bc,__IO_APU_OP_REM16         ; REMOVE 16 bit (single) to z_2
        call asm_am9511a_opp            ; POINTER TO OPERAND IN OPERAND BUFFER

ENDIF                                   ; IF !_DOUBLE

        call asm_am9511a_isr            ; start APU process

        jp asm_am9511a_chk_idle         ; wait for final command

ELSE                                    ; IF !_APU

IF __Z180

   ; signed multiplication of two 16-bit numbers into a 32-bit product.
   ; using the z180 hardware unsigned 8x8 multiply instruction
   ;
   ; enter : de = 16-bit multiplicand = y
   ;         hl = 16-bit multiplier = x
   ;
   ; exit  : dehl = 32-bit product
   ;         carry reset
   ;
   ; uses  : af, bc, de, hl

.l_muls_32_16x16
   ld b,d                       ; d = MSB of multiplicand
   ld c,h                       ; h = MSB of multiplier
   push bc                      ; save sign info

   bit 7,d
   jr Z,l_pos_de                ; take absolute value of multiplicand

   ld a,e
   cpl 
   ld e,a
   ld a,d
   cpl
   ld d,a
   inc de

.l_pos_de
   bit 7,h
   jr Z,l_pos_hl                ; take absolute value of multiplier

   ld a,l
   cpl
   ld l,a
   ld a,h
   cpl
   ld h,a
   inc hl

.l_pos_hl
                                ; prepare unsigned dehl = de x hl
   ld b,l                       ; xl
   ld c,d                       ; yh
   ld d,l                       ; xl
   ld l,c
   push hl                      ; xh yh
   ld l,e                       ; yl

   ; bc = xl yh
   ; de = xl yl
   ; hl = xh yl
   ; stack = xh yh

   mlt de                       ; xl * yl

   mlt bc                       ; xl * yh
   mlt hl                       ; xh * yl
   
   xor a
   add hl,bc                    ; sum cross products
   adc a,a                      ; collect carry

   ld b,a                       ; carry from cross products
   ld c,h                       ; LSB of MSW from cross products

   ld a,d
   add a,l
   ld d,a                       ; de = final product LSW

   pop hl
   mlt hl                       ; xh * yh

   adc hl,bc                    ; hl = final product MSW
   ex de,hl

   pop bc                       ; recover multiplicand/multiplier sign
   ld a,b
   xor c
   ret P                        ; return if positive product

   ld a,l                       ; negate product and return
   cpl
   ld l,a
   ld a,h
   cpl
   ld h,a
   ld a,e
   cpl
   ld e,a
   ld a,d
   cpl
   ld d,a
   inc l
   ret NZ
   inc h
   ret NZ
   inc de
   ret

ELSE                                ; IF !__Z180

;
; Compute DEHL = DE * HL (signed): This routine is not too clever but it
; works. It is based on a standard 16-by-16 multiplication routine for unsigned
; integers. At the beginning the sign of the result is determined based on the
; signs of the operands which are negated if necessary. Then the unsigned
; multiplication takes place, followed by negating the result if necessary.
;

.l_muls_32_16x16
       ld b,d                       ; d = MSB of multiplicand
       ld c,h                       ; h = MSB of multiplier
       push bc                      ; save sign info

       bit 7,d
       jr Z,l_pos_de                ; take absolute value of multiplicand

       ld a,e
       cpl 
       ld e,a
       ld a,d
       cpl
       ld d,a
       inc de

.l_pos_de
       bit 7,h
       jr Z,l_pos_hl                ; take absolute value of multiplier

       ld a,l
       cpl
       ld l,a
       ld a,h
       cpl
       ld h,a
       inc hl

.l_pos_hl
                                     ; prepare unsigned dehl = de x hl
.l_mulu_32_16x16
        ; multiplication of two 16-bit numbers into a 32-bit product
        ;
        ; enter : de = 16-bit multiplicand = y
        ;         hl = 16-bit multiplicand = x
        ;
        ; exit  : dehl = 32-bit product
        ;         carry reset
        ;
        ; uses  : af, bc, de, hl

        ld b,l                      ; x0
        ld c,e                      ; y0
        ld e,l                      ; x0
        ld l,d
        push hl                     ; x1 y1
        push bc                     ; x0 y0        
        ld l,c                      ; y0

        ; de = y1 x0
        ; hl = x1 y0
        ; stack = x1 y1
        ; stack = x0 y0

        call l_z80_mulu_de          ; y1*x0
        ex de,hl
        call l_z80_mulu_de          ; x1*y0

        xor a                       ; zero A
        add hl,de                   ; sum cross products p2 p1
        adc a,a                     ; capture carry p3

        pop de                      ; x0 y0
        ex af,af
        call l_z80_mulu_de          ; y0*x0
        ex af,af
        ld b,a                      ; carry from cross products
        ld c,h                      ; LSB of MSW from cross products

        ld a,d
        add a,l
        ld h,a
        ld l,e                      ; LSW in HL p1 p0

        pop de                      ; x1 y1
        push bc
        ex af,af
        call l_z80_mulu_de          ; x1*y1
        ex af,af
        pop bc
        ex de,hl
        adc hl,bc
        ex de,hl                    ; de = final MSW

;------------------

        pop bc                       ; recover multiplicand/multiplier sign
        ld a,b
        xor c
        ret P                        ; return if positive product

        ld a,l                       ; negate product and return
        cpl
        ld l,a
        ld a,h
        cpl
        ld h,a
        ld a,e
        cpl
        ld e,a
        ld a,d
        cpl
        ld d,a
        inc l
        ret NZ
        inc h
        ret NZ
        inc de
        ret

;------------------------------------------------------------------------------
;
; Fast mulu_16_8x8 using a 512 byte table
;
; x*y = ((x+y)/2)2 - ((x-y)/2)2           <- if x+y is even 
;     = ((x+y-1)/2)2 - ((x-y-1)/2)2 + y   <- if x+y is odd and x>=y
;
; enter : d = 8-bit multiplicand
;         e = 8-bit multiplicand
;
; uses  : af
;
; exit  : de = 16-bit product

.l_z80_mulu_de
        ld a,d                  ; put largest in d
        cp e
        jr NC,lnc
        ld d,e
        ld e,a

.lnc                            ; with largest in d
        xor a
        or e
        jr Z,lzeroe             ; multiply by 0

        ld b,d                  ; keep larger -> b
        ld c,e                  ; keep smaller -> c

        ld a,d
        sub e
        rra
        ld d,a                  ; (x-y)/2 -> d

        ld a,b
        add a,c
        rra                     ; check for odd/even

        push hl                 ; preserve hl

        ld l,a                  ; (x+y)/2 -> l
        ld h,sqrlo/$100         ; loads sqrlo page
        ld a,(hl)               ; LSB ((x+y)/2)2 -> a
        ld e,l                  ; (x+y)/2 -> e
        ld l,d                  ; (x-y)/2 -> l (for index)
        jr NC,leven
                                ; odd tail
        sub (hl)                ; LSB ((x+y)/2)2 - ((x-y)/2)2
        ld l,e                  ; (x+y)/2 -> l
        ld e,a                  ; LSB ((x+y)/2)2 - ((x-y)/2)2 -> e
        inc h                   ; loads sqrhi page
        ld a,(hl)               ; MSB ((x+y)/2)2 -> a
        ld l,d                  ; (x-y)/2 -> l
        sbc a,(hl)              ; MSB ((x+y)/2)2 - ((x-y)/2)2 -> a
        ld d,a                  ; MSB ((x+y)/2)2 - ((x-y)/2)2 -> d

        ld a,e
        add a,c                 ; add smaller y
        ld e,a
        ld a,d
        adc a,0
        ld d,a

        pop hl
        ret

.leven                          ; even tail
        sub (hl)                ; LSB ((x+y)/2)2 - ((x-y)/2)2
        ld l,e                  ; (x+y)/2 -> l
        ld e,a                  ; LSB ((x+y)/2)2 - ((x-y)/2)2 -> e
        inc h                   ; loads sqrhi page
        ld a,(hl)               ; MSB ((x+y)/2)2 -> a
        ld l,d                  ; (x-y)/2 -> l
        sbc a,(hl)              ; MSB ((x+y)/2)2 - ((x-y)/2)2 -> a
        ld d,a                  ; MSB ((x+y)/2)2 - ((x-y)/2)2 -> d

        pop hl
        ret

.lzeroe
        ld d,e
        ret


SECTION rodata_align_256

ALIGN $100

.sqrlo ;low(x*x) should located on the page border
        defb $00,$01,$04,$09,$10,$19,$24,$31,$40,$51,$64,$79,$90,$a9,$c4,$e1
        defb $00,$21,$44,$69,$90,$b9,$e4,$11,$40,$71,$a4,$d9,$10,$49,$84,$c1
        defb $00,$41,$84,$c9,$10,$59,$a4,$f1,$40,$91,$e4,$39,$90,$e9,$44,$a1
        defb $00,$61,$c4,$29,$90,$f9,$64,$d1,$40,$b1,$24,$99,$10,$89,$04,$81
        defb $00,$81,$04,$89,$10,$99,$24,$b1,$40,$d1,$64,$f9,$90,$29,$c4,$61
        defb $00,$a1,$44,$e9,$90,$39,$e4,$91,$40,$f1,$a4,$59,$10,$c9,$84,$41
        defb $00,$c1,$84,$49,$10,$d9,$a4,$71,$40,$11,$e4,$b9,$90,$69,$44,$21
        defb $00,$e1,$c4,$a9,$90,$79,$64,$51,$40,$31,$24,$19,$10,$09,$04,$01
        defb $00,$01,$04,$09,$10,$19,$24,$31,$40,$51,$64,$79,$90,$a9,$c4,$e1
        defb $00,$21,$44,$69,$90,$b9,$e4,$11,$40,$71,$a4,$d9,$10,$49,$84,$c1
        defb $00,$41,$84,$c9,$10,$59,$a4,$f1,$40,$91,$e4,$39,$90,$e9,$44,$a1
        defb $00,$61,$c4,$29,$90,$f9,$64,$d1,$40,$b1,$24,$99,$10,$89,$04,$81
        defb $00,$81,$04,$89,$10,$99,$24,$b1,$40,$d1,$64,$f9,$90,$29,$c4,$61
        defb $00,$a1,$44,$e9,$90,$39,$e4,$91,$40,$f1,$a4,$59,$10,$c9,$84,$41
        defb $00,$c1,$84,$49,$10,$d9,$a4,$71,$40,$11,$e4,$b9,$90,$69,$44,$21
        defb $00,$e1,$c4,$a9,$90,$79,$64,$51,$40,$31,$24,$19,$10,$09,$04,$01
.sqrhi ;high(x*x) located on next page (automatically)
        defb $00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00
        defb $01,$01,$01,$01,$01,$01,$01,$02,$02,$02,$02,$02,$03,$03,$03,$03
        defb $04,$04,$04,$04,$05,$05,$05,$05,$06,$06,$06,$07,$07,$07,$08,$08
        defb $09,$09,$09,$0a,$0a,$0a,$0b,$0b,$0c,$0c,$0d,$0d,$0e,$0e,$0f,$0f
        defb $10,$10,$11,$11,$12,$12,$13,$13,$14,$14,$15,$15,$16,$17,$17,$18
        defb $19,$19,$1a,$1a,$1b,$1c,$1c,$1d,$1e,$1e,$1f,$20,$21,$21,$22,$23
        defb $24,$24,$25,$26,$27,$27,$28,$29,$2a,$2b,$2b,$2c,$2d,$2e,$2f,$30
        defb $31,$31,$32,$33,$34,$35,$36,$37,$38,$39,$3a,$3b,$3c,$3d,$3e,$3f
        defb $40,$41,$42,$43,$44,$45,$46,$47,$48,$49,$4a,$4b,$4c,$4d,$4e,$4f
        defb $51,$52,$53,$54,$55,$56,$57,$59,$5a,$5b,$5c,$5d,$5f,$60,$61,$62
        defb $64,$65,$66,$67,$69,$6a,$6b,$6c,$6e,$6f,$70,$72,$73,$74,$76,$77
        defb $79,$7a,$7b,$7d,$7e,$7f,$81,$82,$84,$85,$87,$88,$8a,$8b,$8d,$8e
        defb $90,$91,$93,$94,$96,$97,$99,$9a,$9c,$9d,$9f,$a0,$a2,$a4,$a5,$a7
        defb $a9,$aa,$ac,$ad,$af,$b1,$b2,$b4,$b6,$b7,$b9,$bb,$bd,$be,$c0,$c2
        defb $c4,$c5,$c7,$c9,$cb,$cc,$ce,$d0,$d2,$d4,$d5,$d7,$d9,$db,$dd,$df
        defb $e1,$e2,$e4,$e6,$e8,$ea,$ec,$ee,$f0,$f2,$f4,$f6,$f8,$fa,$fc,$fe

ENDIF                           ; IF __Z180

ENDIF                           ; IF _APU

.delay
        push bc
        ld b, $00
.delay_loop
        ex (sp), hl
        ex (sp), hl
        djnz delay_loop
        pop bc
        ret
