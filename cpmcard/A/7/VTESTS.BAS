10 E$=CHR$(27):Q$=CHR$(34)
20 WIDTH 255
100 GOSUB 9300:PRINT E$;"[2J";E$;"[H";
110 PRINT "VT TEST MENU"
120 PRINT "1 Ink colors (normal / ALT)"
130 PRINT "2 Paper colors"
140 PRINT "3 Attributes"
150 PRINT "4 DEC special graphics"
160 PRINT "5 VT52 mode"
170 PRINT "6 80 columns / manual pan"
180 PRINT "7 Combined ANSI test"
185 PRINT "8 Mixworx code / LSB-like procedure"
187 PRINT "9 Scroll regions (80x24 logical)"
190 PRINT "A Run all (automatic)"
200 PRINT "Q Return to CP/M"
210 LINE INPUT "Select: ";C$
220 IF C$="Q" OR C$="q" THEN SYSTEM
230 AU=0
240 IF C$="A" OR C$="a" THEN AU=1:GOTO 400
250 IF LEN(C$)<>1 THEN 100
260 N=ASC(C$)-48
270 IF N<1 OR N>9 THEN 100
280 GOSUB 8000
290 GOTO 100
400 FOR N=1 TO 9
410 GOSUB 8000
420 NEXT N
430 PRINT E$;"[0m";E$;"[2J";E$;"[H";
440 PRINT "ALL VT TESTS COMPLETE"
450 AU=0:GOSUB 9000
460 GOTO 100
1000 PRINT E$;"[0m";E$;"[2J";E$;"[H";
1010 PRINT "INK PALETTE: normal / ALT"
1015 PRINT "Index letter name (ANSI SGR)"
1020 PRINT E$;"[3;1H";E$;"[30m 0 i implicit (30)"
1030 PRINT E$;"[3;19H";E$;"[90m 8 I ALT IMPLICIT (90)"
1040 PRINT E$;"[4;1H";E$;"[31m 1 r red (31)"
1050 PRINT E$;"[4;19H";E$;"[91m 9 R ALT RED (91)"
1060 PRINT E$;"[5;1H";E$;"[32m 2 g green (32)"
1070 PRINT E$;"[5;19H";E$;"[92m10 G ALT GREEN (92)"
1080 PRINT E$;"[6;1H";E$;"[34m 3 b blue (34)"
1090 PRINT E$;"[6;19H";E$;"[94m11 B ALT BLUE (94)"
1100 PRINT E$;"[7;1H";E$;"[37m 4 s shade (37)"
1110 PRINT E$;"[7;19H";E$;"[97m12 S ALT SHADE (97)"
1120 PRINT E$;"[8;1H";E$;"[36m 5 c cyan (36)"
1130 PRINT E$;"[8;19H";E$;"[96m13 C ALT CYAN (96)"
1140 PRINT E$;"[9;1H";E$;"[35m 6 m magenta (35)"
1150 PRINT E$;"[9;19H";E$;"[95m14 M ALT MAGENTA (95)"
1160 PRINT E$;"[10;1H";E$;"[33m 7 y yellow (33)"
1170 PRINT E$;"[10;19H";E$;"[93m15 Y ALT YELLOW (93)"
1190 GOSUB 9100
1200 RETURN
2000 PRINT E$;"[2J";E$;"[H";"PAPER COLORS"
2010 PRINT E$;"[3;1H";E$;"[47;30mIMPLICIT";E$;"[0m"
2020 PRINT E$;"[5;1H";E$;"[101;30mRED MARKER";E$;"[0m"
2030 PRINT E$;"[7;1H";E$;"[102;30mGREEN MARKER";E$;"[0m"
2040 PRINT E$;"[9;1H";E$;"[104;30mBLUE MARKER";E$;"[0m"
2050 GOSUB 9100
2060 RETURN
3000 PRINT E$;"[2J";E$;"[H";"ATTRIBUTES"
3010 PRINT E$;"[3;1HNORMAL: steady ASCII, no graphics"
3020 PRINT E$;"[5;1H";E$;"[1mBOLD";E$;"[22m / normal"
3030 PRINT E$;"[7;1H";E$;"[4mUNDERLINE";E$;"[24m / normal"
3040 PRINT E$;"[9;1H";E$;"[5mBLINK";E$;"[25m / steady"
3050 PRINT E$;"[11;1H";E$;"[7mREVERSE";E$;"[27m / normal"
3055 PRINT E$;"[12;1HOnly BLINK should disappear/reappear."
3060 GOSUB 9100
3070 RETURN
4000 PRINT CHR$(15);E$;"(B";E$;")B";E$;"[0m";
4010 PRINT E$;"[2J";E$;"[H";"DEC GRAPHICS 1/2: JOINS AND PATTERNS"
4020 H$=STRING$(11,"q"):V$=SPACE$(11)
4030 PRINT E$;"[3;1H";E$;"(0l";H$;"w";H$;"w";H$;"k"
4040 FOR R=4 TO 8
4050 PRINT E$;"[";MID$(STR$(R),2);";1Hx";V$;"x";V$;"x";V$;"x"
4060 NEXT R
4070 PRINT E$;"[6;1Ht";H$;"n";H$;"n";H$;"u"
4080 PRINT E$;"[9;1Hm";H$;"v";H$;"v";H$;"j";E$;"(B"
4090 PRINT E$;"[4;3HG0 frame";E$;"[4;15Hjoins";E$;"[4;28Hcorners"
4100 PRINT E$;"[10;1HG1 SO/SI: ";E$;")0";CHR$(14);"lqqk x n";
4110 PRINT CHR$(15);" ASCII qx";E$;")B"
4120 PRINT E$;"[11;1HPattern: ";E$;"(0aaaaaa ` fg ~";E$;"(B"
4130 PRINT E$;"[12;1HScanlines: ";E$;"(0oo pp qq rr ss";E$;"(B"
4140 PRINT E$;"[13;1Hb c d e h i: missing; pi/GBP: approx"
4150 PRINT E$;"[0m";E$;"[14;1H";
4152 IF AU=1 THEN GOSUB 9050:GOTO 4160
4154 LINE INPUT "Enter: all 32 glyphs ";A$
4160 PRINT E$;"[2J";E$;"[H";"DEC ALL 32: HEX / ASCII / GLYPH"
4170 FOR K=0 TO 3:R=2+K*3
4180 PRINT E$;"[";MID$(STR$(R),2);";1H";
4190 FOR J=0 TO 7:PRINT HEX$(95+K*8+J);" ";:NEXT J:PRINT
4200 PRINT E$;"[";MID$(STR$(R+1),2);";1H";
4210 FOR J=0 TO 7:PRINT CHR$(95+K*8+J);"  ";:NEXT J:PRINT
4220 PRINT E$;"[";MID$(STR$(R+2),2);";1H";E$;"(0";
4230 FOR J=0 TO 7:PRINT CHR$(95+K*8+J);"  ";:NEXT J
4240 PRINT E$;"(B":NEXT K
4250 PRINT CHR$(15);E$;"(B";E$;")B";
4260 GOSUB 9100
4270 RETURN
5000 PRINT E$;"[?2l";E$;"H";E$;"J";"VT52: ADDRESS / MOVE / ERASE"
5010 R=3:C=1:GOSUB 5500:PRINT "ABCD moves: [    ]";
5020 R=3:C=14:GOSUB 5500:PRINT E$;"C";E$;"D";"P";
5030 PRINT E$;"B";E$;"A";"ASS";
5040 R=5:C=1:GOSUB 5500:PRINT "Erase K: KEEP REMOVE";
5050 R=5:C=14:GOSUB 5500:PRINT E$;"K";
5060 R=7:C=1:GOSUB 5500:PRINT "Erase J: KEEP";
5070 R=8:C=1:GOSUB 5500:PRINT "REMOVE BELOW";
5080 R=7:C=14:GOSUB 5500:PRINT E$;"J";
5090 R=9:C=1:GOSUB 5500:PRINT "No REMOVE text should remain."
5100 R=11:C=1:GOSUB 5500:PRINT E$;"Flqqk";E$;"G ASCII qx"
5110 R=12:C=1:GOSUB 5500:PRINT "F/G: graphics then literal ASCII."
5120 R=14:C=1:GOSUB 5500
5130 IF AU=1 THEN GOSUB 9050:GOTO 5150
5140 LINE INPUT "Enter: return to ANSI ";A$
5150 PRINT E$;"<":GOSUB 9300
5160 RETURN
5500 PRINT E$;"Y";CHR$(31+R);CHR$(31+C);:RETURN
6000 PRINT E$;"[2J";E$;"[H";"80 COLUMNS / MANUAL PAN"
6010 PRINT E$;"[4;1H";"LEFT EDGE";E$;"[4;71H";"RIGHT EDGE"
6020 PRINT E$;"[7;1H";"0123456789";E$;"[7;71H";"RIGHT"
6030 PRINT E$;"[10;1H";"Use horizontal pan keys / Ctrl+] a/e"
6040 GOSUB 9000
6050 RETURN
7000 PRINT E$;"[0m";E$;"[2J";E$;"[H";
7010 PRINT "7xtgv ANSI COLOR TEST"
7020 PRINT E$;"[4;1H";"80 COLUMNS:"
7030 FOR I=1 TO 80
7040 PRINT CHR$(48+(I MOD 10));
7050 NEXT I
7060 PRINT
7070 PRINT E$;"[6;1H";E$;"[31mNORMAL RED";E$;"[0m"
7080 PRINT E$;"[7;1H";E$;"[91mALT RED";E$;"[0m"
7090 PRINT E$;"[8;1H";E$;"[4;34mUNDERLINE";E$;"[0m"
7100 PRINT E$;"[9;1H";E$;"[5;35mBLINK";E$;"[0m / steady"
7110 PRINT E$;"[10;1H";E$;"[7;36mREVERSE";E$;"[0m / normal"
7120 PRINT E$;"[12;1H";E$;"(0l";
7130 PRINT STRING$(78,"q");"k";E$;"(B";
7140 FOR R=13 TO 19
7150 PRINT E$;"[";MID$(STR$(R),2);";1H";
7160 PRINT E$;"(0x";E$;"[";MID$(STR$(R),2);";80Hx";
7170 PRINT E$;"(B";
7180 NEXT R
7190 PRINT E$;"[20;1H";E$;"(0m";
7200 PRINT STRING$(78,"q");"j";E$;"(B";
7210 GOSUB 9000
7220 RETURN
8000 GOSUB 9300
8010 ON N GOSUB 1000,2000,3000,4000,5000,6000,7000,10000,12000
8020 GOSUB 9300:RETURN
9000 PRINT E$;"[0m";E$;"[22;1H";
9010 IF AU=1 THEN 9050
9020 LINE INPUT "Enter returns to menu: ";A$
9030 RETURN
9050 PRINT "Next test shortly...";
9060 FOR W=1 TO 18000:NEXT W
9070 RETURN
9100 PRINT E$;"[0m";E$;"[14;1H";:GOTO 9010
9200 PRINT E$;"[?6l";E$;"[24;1H";E$;"[K";
9210 IF AU=1 THEN GOSUB 9050:RETURN
9220 LINE INPUT "Enter: next scroll check ";A$:RETURN
9300 PRINT E$;"<";CHR$(15);E$;"(B";E$;")B";E$;"[0m";
9310 PRINT E$;"[?6l";E$;"[r";:RETURN
10000 REM MIXWORX SEMANTIC COLOR CODE FIXTURE
10010 PRINT E$;"[0;30m";E$;"[2J";E$;"[H";
10020 PRINT "MIXWORX CODE / LSB-LIKE PROCEDURE"
10030 PRINT E$;"[3;1H";E$;"[1;30m";"defproc"
10040 PRINT E$;"[3;8H";E$;"[22;30m";" "
10050 PRINT E$;"[3;9H";E$;"[22;35m";"greet_users"
10060 PRINT E$;"[3;20H";E$;"[22;34m";"("
10070 PRINT E$;"[3;21H";E$;"[22;35m";"names$"
10080 PRINT E$;"[3;27H";E$;"[22;34m";","
10090 PRINT E$;"[3;28H";E$;"[22;30m";" "
10100 PRINT E$;"[3;29H";E$;"[22;35m";"count"
10110 PRINT E$;"[3;34H";E$;"[22;34m";")"
10120 PRINT E$;"[4;1H";E$;"[22;30m";"  "
10130 PRINT E$;"[4;3H";E$;"[1;30m";"local"
10140 PRINT E$;"[4;8H";E$;"[22;30m";" "
10150 PRINT E$;"[4;9H";E$;"[22;35m";"i"
10160 PRINT E$;"[4;10H";E$;"[22;34m";","
10170 PRINT E$;"[4;11H";E$;"[22;30m";" "
10180 PRINT E$;"[4;12H";E$;"[22;35m";"label$"
10190 PRINT E$;"[4;18H";E$;"[22;34m";","
10200 PRINT E$;"[4;19H";E$;"[22;30m";" "
10210 PRINT E$;"[4;20H";E$;"[22;35m";"total"
10220 PRINT E$;"[6;1H";E$;"[22;32m";"  rem Mixworx: a small greeting loop"
10230 PRINT E$;"[7;1H";E$;"[22;30m";"  "
10240 PRINT E$;"[7;3H";E$;"[22;35m";"total"
10250 PRINT E$;"[7;8H";E$;"[22;30m";" "
10260 PRINT E$;"[7;9H";E$;"[22;34m";"="
10270 PRINT E$;"[7;10H";E$;"[22;30m";" "
10280 PRINT E$;"[7;11H";E$;"[22;31m";"0"
10290 PRINT E$;"[8;1H";E$;"[22;30m";"  "
10300 PRINT E$;"[8;3H";E$;"[1;30m";"for"
10310 PRINT E$;"[8;6H";E$;"[22;30m";" "
10320 PRINT E$;"[8;7H";E$;"[22;35m";"i"
10330 PRINT E$;"[8;8H";E$;"[22;30m";" "
10340 PRINT E$;"[8;9H";E$;"[22;34m";"="
10350 PRINT E$;"[8;10H";E$;"[22;30m";" "
10360 PRINT E$;"[8;11H";E$;"[22;31m";"1"
10370 PRINT E$;"[8;12H";E$;"[22;30m";" "
10380 PRINT E$;"[8;13H";E$;"[1;30m";"to"
10390 PRINT E$;"[8;15H";E$;"[22;30m";" "
10400 PRINT E$;"[8;16H";E$;"[22;35m";"count"
10410 PRINT E$;"[9;1H";E$;"[22;30m";"    "
10420 PRINT E$;"[9;5H";E$;"[22;35m";"label$"
10430 PRINT E$;"[9;11H";E$;"[22;30m";" "
10440 PRINT E$;"[9;12H";E$;"[22;34m";"="
10450 PRINT E$;"[9;13H";E$;"[22;30m";" "
10460 PRINT E$;"[9;14H";E$;"[22;31m";"";Q$;"Hello, ";Q$;""
10470 PRINT E$;"[9;23H";E$;"[22;30m";" "
10480 PRINT E$;"[9;24H";E$;"[22;34m";"&"
10490 PRINT E$;"[9;25H";E$;"[22;30m";" "
10500 PRINT E$;"[9;26H";E$;"[22;35m";"names$"
10510 PRINT E$;"[9;32H";E$;"[22;34m";"("
10520 PRINT E$;"[9;33H";E$;"[22;35m";"i"
10530 PRINT E$;"[9;34H";E$;"[22;34m";")"
10540 PRINT E$;"[10;1H";E$;"[22;30m";"    "
10550 PRINT E$;"[10;5H";E$;"[1;30m";"if"
10560 PRINT E$;"[10;7H";E$;"[22;30m";" "
10570 PRINT E$;"[10;8H";E$;"[22;35m";"names$"
10580 PRINT E$;"[10;14H";E$;"[22;34m";"("
10590 PRINT E$;"[10;15H";E$;"[22;35m";"i"
10600 PRINT E$;"[10;16H";E$;"[22;34m";")"
10610 PRINT E$;"[10;17H";E$;"[22;30m";" "
10620 PRINT E$;"[10;18H";E$;"[22;34m";"<"
10630 PRINT E$;"[10;19H";E$;"[22;34m";">"
10640 PRINT E$;"[10;20H";E$;"[22;30m";" "
10650 PRINT E$;"[10;21H";E$;"[22;31m";"";Q$;"";Q$;""
10660 PRINT E$;"[10;23H";E$;"[22;30m";" "
10670 PRINT E$;"[10;24H";E$;"[1;30m";"then"
10680 PRINT E$;"[11;1H";E$;"[22;30m";"      "
10690 PRINT E$;"[11;7H";E$;"[1;30m";"print"
10700 PRINT E$;"[11;12H";E$;"[22;30m";" "
10710 PRINT E$;"[11;13H";E$;"[22;35m";"label$"
10720 PRINT E$;"[12;1H";E$;"[22;30m";"      "
10730 PRINT E$;"[12;7H";E$;"[22;35m";"total"
10740 PRINT E$;"[12;12H";E$;"[22;30m";" "
10750 PRINT E$;"[12;13H";E$;"[22;34m";"="
10760 PRINT E$;"[12;14H";E$;"[22;30m";" "
10770 PRINT E$;"[12;15H";E$;"[22;35m";"total"
10780 PRINT E$;"[12;20H";E$;"[22;30m";" "
10790 PRINT E$;"[12;21H";E$;"[22;34m";"+"
10800 PRINT E$;"[12;22H";E$;"[22;30m";" "
10810 PRINT E$;"[12;23H";E$;"[22;31m";"1"
10820 PRINT E$;"[13;1H";E$;"[22;30m";"    "
10830 PRINT E$;"[13;5H";E$;"[1;30m";"else"
10840 PRINT E$;"[14;1H";E$;"[22;30m";"      "
10850 PRINT E$;"[14;7H";E$;"[1;30m";"print"
10860 PRINT E$;"[14;12H";E$;"[22;30m";" "
10870 PRINT E$;"[14;13H";E$;"[22;31m";"";Q$;"Guest #";Q$;""
10880 PRINT E$;"[14;22H";E$;"[22;34m";";"
10890 PRINT E$;"[14;23H";E$;"[22;30m";" "
10900 PRINT E$;"[14;24H";E$;"[22;35m";"i"
10910 PRINT E$;"[15;1H";E$;"[22;30m";"    "
10920 PRINT E$;"[15;5H";E$;"[1;30m";"endif"
10930 PRINT E$;"[16;1H";E$;"[22;30m";"  "
10940 PRINT E$;"[16;3H";E$;"[1;30m";"endfor"
10950 PRINT E$;"[16;9H";E$;"[22;30m";" "
10960 PRINT E$;"[16;10H";E$;"[22;35m";"i"
10970 PRINT E$;"[18;1H";E$;"[22;32m";"  rem Report how many names were greeted"
10980 PRINT E$;"[19;1H";E$;"[22;30m";"  "
10990 PRINT E$;"[19;3H";E$;"[1;30m";"print"
11000 PRINT E$;"[19;8H";E$;"[22;30m";" "
11010 PRINT E$;"[19;9H";E$;"[22;31m";"";Q$;"Greeted: ";Q$;""
11020 PRINT E$;"[19;20H";E$;"[22;34m";";"
11030 PRINT E$;"[19;21H";E$;"[22;30m";" "
11040 PRINT E$;"[19;22H";E$;"[22;35m";"total"
11050 PRINT E$;"[20;1H";E$;"[1;30m";"endproc"
11060 PRINT E$;"[20;8H";E$;"[22;30m";" "
11070 PRINT E$;"[20;9H";E$;"[22;35m";"greet_users"
11080 GOSUB 9000
11090 RETURN
12000 REM SCROLL CHECKS REQUIRE EXACTLY 80X24 LOGICAL CELLS
12010 GOSUB 9300:PRINT E$;"[2J";E$;"[H";"HEADER stays at row 1"
12020 PRINT E$;"[23;1HFOOTER stays at row 23";
12030 PRINT E$;"[2;22r";E$;"[2;1H";
12040 FOR R=1 TO 25:PRINT "SCROLL";R:NEXT R
12050 PRINT E$;"[23;32H1/5: rows 2..21 = 6..25";
12060 GOSUB 9200
12070 PRINT E$;"[2;1H";E$;"M";
12080 PRINT E$;"[23;32H2/5: top blank; 6 at row 3";
12090 GOSUB 9200
12100 FOR R=2 TO 22
12110 PRINT E$;"[";MID$(STR$(R),2);";1H";E$;"[KROW";R;
12120 NEXT R
12130 PRINT E$;"[10;1H";E$;"[3L";E$;"[2M";
12140 PRINT E$;"[23;32H3/5: row 10 blank, 11=ROW 10";
12150 GOSUB 9200
12160 PRINT E$;"[5;20r";E$;"[?6h";E$;"[1;1HTOP";E$;"[K";
12170 PRINT E$;"[16;1HBOTTOM";E$;"[K";
12180 PRINT E$;"[999A";E$;"[1G";"TOP CLAMP";E$;"[K";
12190 PRINT E$;"[999B";E$;"[1G";"BOTTOM CLAMP";E$;"[K";
12200 PRINT E$;"[?6l";E$;"[23;32H4/5: TOP=5, BOTTOM=20";E$;"[K";
12210 GOSUB 9200
12220 GOSUB 9300
12230 PRINT E$;"[23;1H5/5: expect ROW 2 at top; this at row 22";E$;"[K";
12240 PRINT E$;"[24;1H";E$;"[K";"FULL-SCREEN SCROLL"
12250 GOSUB 9200:GOSUB 9300:RETURN
