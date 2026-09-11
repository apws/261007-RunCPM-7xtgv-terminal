; REBOOT.COM - 7xtgv RunCPM ESP32-S3 whole-device reboot.
; Clears the terminal, announces a three-second delay, then requests private
; host port 7Ch. The delay allows 7XTGVTRM.BUF to finish its lazy SD save.
        org     0100h
        ld      de,clear_screen
        ld      c,9
        call    5
        ld      de,message
        ld      c,9
        call    5
        ld      a,1
        out     (7ch),a
        ret
clear_screen:
        db      27,'[2J',27,'[H','$'
message: db      '7xtgv reboot in 3 seconds...',13,10,'$'
