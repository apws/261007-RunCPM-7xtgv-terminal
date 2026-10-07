10 E$=CHR$(27)
20 PRINT E$;"[0m";E$;"[2J";E$;"[H";
30 PRINT "7xtgv VT100 COLOR MATRIX";
40 PRINT E$;"[5;1H";"WHITE: ";E$;"[47;31m";"RED";E$;"[47;32m";"GRE";E$;"[47;33m";"YEL";E$;"[47;34m";"BLU";E$;"[47;35m";"MAG";E$;"[47;36m";"CYA";E$;"[47;37m";"WHI";E$;"[47;90m";"GRA";
50 PRINT E$;"[8;1H";"PALE RED: ";E$;"[101;31m";"RED";E$;"[101;32m";"GRE";E$;"[101;33m";"YEL";E$;"[101;34m";"BLU";E$;"[101;35m";"MAG";E$;"[101;36m";"CYA";E$;"[101;37m";"WHI";E$;"[101;90m";"GRA";
60 PRINT E$;"[11;1H";"PALE GREEN: ";E$;"[102;31m";"RED";E$;"[102;32m";"GRE";E$;"[102;33m";"YEL";E$;"[102;34m";"BLU";E$;"[102;35m";"MAG";E$;"[102;36m";"CYA";E$;"[102;37m";"WHI";E$;"[102;90m";"GRA";
70 PRINT E$;"[14;1H";"PALE BLUE: ";E$;"[104;31m";"RED";E$;"[104;32m";"GRE";E$;"[104;33m";"YEL";E$;"[104;34m";"BLU";E$;"[104;35m";"MAG";E$;"[104;36m";"CYA";E$;"[104;37m";"WHI";E$;"[104;90m";"GRA";
80 PRINT E$;"[18;1H";E$;"[1;31mBOLD DARK RED";E$;"[0m  ";E$;"[4;34mUNDERLINE";E$;"[0m";
90 PRINT E$;"[20;1H";E$;"[5;35mBLINK MAGENTA";E$;"[0m  ";E$;"[7;36mREVERSE CYAN";E$;"[0m";
100 PRINT E$;"[22;1H";"80 COLUMNS: LEFT";E$;"[22;71H";"RIGHT";
110 A$=INPUT$(1)
120 SYSTEM
;"[7;36mREV