erase
clear
clear gets
set escape on
set talk off
set raw off
text
  CelŸ program se skl¡d¡ jen z jednoho souboru CMD, v”e ostatn… se

  vytvo“… automaticky. Pro spr¡vnou  funkci je v”ak nutno pracovat

  korektn≈. Program je ps¡n pro RAMDISK, ale nen… to podm…nkou. RD

  z¡lohovanŸ i nez¡lohovanŸ si um… zjistit. Pokud  je RD p“…tomen,

  v”echny vŸsledky  se automaticky ukl¡daj… na n≈j, i kdy⁄ program

  s¡m je spu”t≈n z jin◊ho disku. P“i nep“…tomnosti RD  se  ve”ker◊ 

  vŸsledky ukl¡daj… na disk, z kter◊ho byl spu”t≈n program AR.CMD.

  Program  si  ale  neum… hl…dat kapacitu disku, proto je nutn◊ si

  dostatek voln◊ho m…sta zajistit p“edem.

  Nyn… si otestuji Tv j syst◊m a mo⁄n¡ Ti d¡m n≈jak◊ ot¡zky.

                                                 M≈j tedy strpen….

    P“epracoval: M.Urb¡nek,Brno       Zm¡√kni cokoliv.......
endtext
@ 22,0
set console off
wait
set console on
erase
store F to RAMDISK
store peek(59967) to RD
if RD>0 .and. RD<=64
store T to RAMDISK
endif

store T to INIT
store 'AR' to DISKAR
 do while INIT
 store file ('&DISKAR') to TESTAR
  if TESTAR
  store F to INIT
  else
  erase
   store ' ' to DISK 
   @ 3,3 say 'Nena”el jsem na disku soubor AR.DBF, pot“ebuji v≈d≈t'
   @ 5,3 say 'na kter◊m disku se nach¡z… !!!' 
   @ 8,3 say 'Zadej n¡zev disku s t…mto prg. (A - E) ' get DISK picture;
   'A'
   read
   store !(DISK)+':AR' to DISKAR
   loop
   endif
 enddo    
 
  if RAMDISK
  store 'E:ARP' to PARP
  store 'E:ARV' to PARV
  store 'E:ARD' to PARD
  store 'E:AR' to MAR
  store 'ramdisk' to TEXT
  else 
  store 'ARP' to PARP
  store 'ARV' to PARV
  store 'ARD' to PARD
  store 'AR' to MAR
  store 'disk' to TEXT
  endif

 use &DISKAR
 copy structure to &PARP
 use

  If .NOT. file('&MAR'+'MEM')
  store '(!(trim(KNazev)) $ !(Nazev))' to P1
  store '(Cislo = val(KCislo))'        to P2
  store '(Rok = val(KRok))'            to P3
  save to &MAR
  endif
 set colon off
 store ' ' to VOLBA
 store 0 to POCETVET
 store 0 to ZRUS
do while VOLBA <> '6'
   erase
text

                   P“ehled √l¡nk  √asopisu Amat◊rsk◊ r¡dio
                   ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~



                         1 - VŸb≈r podle kl…√e

                         2 - Prohl…⁄en… vybranŸch v≈t

                         3 - Zru”en… ozna√enŸch v≈t

                         4 - Vytvo“en… tisk. souboru

                         5 - DoplŒov¡n… evidence

                         6 - Konec pr¡ce
endtext
   @ 0,0
   set console off
   wait to VOLBA
   set console on
   do case
      case VOLBA = '1'
           erase
           use &PARP
           copy stru to &PARV
           use &PARV
           restore from &MAR additive
           store '                                        ' to KNazev
           store '  '                                       to KCislo
           store '  '                                       to KRok
           store ' '                                        to Podminka
           set colon on
           @  1,28 say 'Zadej kl…√ pro vyhled¡v¡n…'
           @  2,28 say '~~~~~~~~~~~~~~~~~~~~~~~~~~'
           @  7,15 say 'N¡zev '
           @  7,22 get KNazev
           @  9,15 say '„…slo '
           @  9,22 get KCislo
           @ 11,15 say 'Rok   '
           @ 11,22 get KRok
           read
           set colon off
           if KNazev <> '                                        '
            store P1 to Podminka
           endif
           if KCislo <> '  '
            if Podminka = ' '
             store P2 to Podminka
            else
             store Podminka + ' .AND. ' + P2 to Podminka
            endif
           endif
            if KRok <> '  '
             if Podminka = ' '
              store P3 to Podminka
             else
              store Podminka + ' .AND. ' + P3 to Podminka
             endif
            endif

             if Podminka <> ' '
              store '(' + Podminka + ')' to Podminka
              erase
              text

                *******************************************
                *                                         *
                *  „ekejte, pros…m, prohled¡v¡m datab¡zi  *
                *                                         *
                *******************************************
              endtext
              @ 0,0
              append from &DISKAR for &Podminka
             count to pocetvet
             use
            endif
      case VOLBA = '2'
           erase
           if POCETVET = 0
            @ 6,10 say 'Lituji, ale nebyla vybr¡na ⁄¡dn¡ v≈ta !'
            @ 13,37 say 'Zm¡√kni cokoliv....'
            set console off
            @ 0,0
            wait
            set console on
            loop
           endif 
           use &PARV
           go top
           browse
           count for * to ZRUS
           use
      case VOLBA = '3'
           erase
           if ZRUS = 0
            @ 5,10 say 'Nebyla ozna√ena ⁄¡dn¡ v≈ta ke zru”en… !!!'
           else
            @ 5,10 say 'Pocet v≈t ur√enŸch ke zru”en… je '+str(ZRUS,4)+' !!!'
            use &PARV
            pack
            use
           endif
           @ 13,37 say 'Zm¡√kni cokoliv.....'
           @ 0,0 
           set console off
           wait
           set console on
      case VOLBA = '4'
           erase
           if POCETVET = 0
            @ 6,10 say 'Lituji, ale nebyla vybr¡na ⁄¡dn¡ v≈ta !'
            @ 13,37 say 'Zm¡√kni cokoliv....'
            set console off
            @ 0,0
            wait
            set console on
            loop
           endif
           store 'AR      ' to TISKSOUB
           use &PARV
           go top
           erase
           @ 1,6 say 'VŸslednŸ soubor ulo⁄…m na '+trim(TEXT)+' pod n¡zvem ';
+trim(Tisksoub)+'.TXT'
            store 'A' to ODP
            @ 3,6 say "Chce” zm≈nit jeho n¡zev ? (A/ )"
            @ 3,38 get ODP picture '!'
            read
             if ODP ='A' 
              @ 8,0        
              ? 'Zde je adres¡“ disku :'
              ? '~~~~~~~~~~~~~~~~~~~~~~'
               if RAMDISK
                disp file like *.txt on E:
               else
                disp file like *.txt
               endif
              store '        ' to Tisksoub
              set colon on
              store T to TEST
               do while TEST
                @ 5,6 say '                                               '
                @ 5,6 say 'NovŸ n¡zev souboru bude - 'get Tisksoub picture;
'!!!!!!!!'
                read
                 if rank(TISKSOUB) < 65 .or. rank(TISKSOUB) > 90
                  loop
                 endif
                store F to TEST
               enddo
             set colon off
            endif
           erase
            if RAMDISK
             stor 'E:'+trim(TISKSOUB) to ZTISKSOUB
            else
             store trim(TISKSOUB) to ZTISKSOUB
            endif
           set alternate to &ZTISKSOUB
           set alternate on
           text
============================================================================
N¡zev                                                        AR  √. rok  str
============================================================================
           endtext
           list off
           text
============================================================================
           endtext
           set alternate off
           set alternate to
           erase
@  4,8  say '*****************************************************************'
@  5,8  say '*                                                               *'
@  6,8  say '*          VŸslednŸ tiskovŸ soubor je ulo⁄en pod n¡zvem         *'
@  7,8  say '*                                                               *'
@  8,8  say '*'
           if RAMDISK
@  8,20 say trim(TISKSOUB)+'.TXT na ramdisk !!!!'
           else
@  8,20 say trim(TISKSOUB)+'.TXT na implicitn… disk !!!!'
           endif
@  8,72 say '*'
@  9,8  say '*                                                               *'
@ 10,8  say '*     Je mo⁄n◊ ho vytisknout v jak◊mkoliv textov◊m editoru.     *'
@ 11,8  say '*                                                               *'
@ 12,8  say '*****************************************************************'
@ 16,37 say 'Stiskni libovolnou kl¡vesu..... '
           @ 0,0
           set console off
           wait
           set console on
           use
      case VOLBA = '5'
           if DATE() ='00/00/00'
           store DATE() to DDATE
           erase	
@  5, 0 say "+----------------------------------------------------------------------+"
@  6, 0 say ". Jeliko⁄ n¡” SHARPIK si nem ⁄e pamatovat datum, je pro pot“eby tohoto ."
@  7, 0 say ".                                                                      ."
@  8, 0 say ". programu t“eba jej zadat. Vstup je o”et“en tak, aby nebylo mo⁄n◊ za- ."
@  9, 0 say ".                                                                      ."
@ 10, 0 say ". dat nesmysln◊ datum (dny - m≈s…ce). Rok snad u⁄ ka⁄dŸ zad¡ spr¡vn≈.  ."
@ 11, 0 say "+----------------------------------------------------------------------+"
           @ 14, 4 say"Zadej tedy spr¡vn◊ dne”n… datum (dd/mm/rr):"
           @ 14,49 get DDATE picture '99/99/99'
           READ
           if VAL($(DDATE,4,2)) = 0 .OR. VAL($(DDATE,4,2)) > 12
           ?? chr(7)
           @ 17,15 say "No tak to snad ne !!!"
           @ 0,0
           store 20 to TIME
            do while TIME > 0
            store TIME - 1 to TIME
            enddo
           loop
           endif
           if VAL($(DDATE,4,2)) = 2 
            if VAL($(DDATE,1,2))= 0 .OR. VAL($(DDATE,1,2)) > 29
            ?? chr(7)
             @ 17,15 say "No tak to snad ne !!!"
             @ 0,0
             store 20 to TIME
              do while TIME > 0
              store TIME - 1 to TIME
              enddo
             loop
            endif
           endif
           if VAL($(DDATE,4,2)) = 4 .OR. VAL($(DDATE,4,2)) = 6;
.OR. VAL($(DDATE,4,2)) = 9.OR. VAL($(DDATE,4,2)) = 11
            if VAL($(DDATE,1,2)) = 0 .OR. VAL($(DDATE,1,2)) > 30
             ?? chr(7)
             @ 17,15 say "No tak to snad ne !!!"
             @ 0,0
             store 20 to TIME
              do while TIME > 0
              store TIME - 1 to TIME
              enddo
             loop
             endif 
            endif
            if VAL($(DDATE,4,2)) = 1 .OR. VAL($(DDATE,4,2)) = 3;
.OR. VAL($(DDATE,4,2)) = 5 .OR. VAL($(DDATE,4,2)) = 7;
.OR. VAL($(DDATE,4,2)) = 8 .OR. VAL($(DDATE,4,2)) = 10;
.OR. VAL($(DDATE,4,2)) = 12
             if VAL($(DDATE,1,2)) = 0 .OR. VAL($(DDATE,1,2)) > 31
              ?? chr(7)
              @ 17,15 say "No tak to snad ne !!!"
              @ 0,0
              store 20 to TIME
               do while TIME > 0
               store TIME - 1 to TIME
               enddo
              loop
              endif 
             endif
            set date to &DDATE
           endif
           erase
           use &PARP
           copy structure to &PARD
           use &PARD
           store F to OPRAVIT
           store F to DOPACK
           store T to CYKL
           do while CYKL
            if .NOT. OPRAVIT
             append blank
            endif
            store F to OPRAVIT
            erase
            set colon on
            @  2,18 say "Funkce 5 - zad¡v¡n… novŸch ’daj "
            @  3,17 say "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
            @  5, 2 say "N¡zev √l¡nku :"
            @  7, 5 get NAZEV
            @  9, 2 say "Typ AR :"
            @  9,22 get AR picture 'AA'
            @ 11, 2 say "„…slo AR :"
            @ 11,22 get CISLO
            @ 13, 2 say "Ro√n…k :"
            @ 13,22 get ROK
            @ 15, 2 say "„…slo str¡nky AR :"
            @ 15,22 get STRANA
            set colon off
            read
            replace AR with !(AR)
            store "3" to MENU
            @ 21,1 say "1 - opravit   2 - vymazat   3 - zapsat"
            @ 23,1 say 'odpov≈ƒ  ' get MENU pictu "9"
            read
             do case
              case MENU ="1"
                   store T to OPRAVIT
                   loop
              case MENU="2"
                   store T to DOPACK
                   delete
              endcase
            erase
            store 'A' to VKLAD
            @ 20,15 say 'Dal”… data ? (A/ ) 'get VKLAD picture '!'
            read
             if VKLAD = 'A'
              loop
             else
              store F to CYKL
             endif
           enddo
            if DOPACK 
             erase
             @ 3,10 say 'Teƒ budu mazat to, co jsi zvrznul....'
             @ 5,1 
             display all for * off
             pack
             store 20 to TIME
             do while TIME >0
              store TIME - 1 to TIME
             enddo       
            endif
           count all to ZAPIS
            if ZAPIS > 0
             erase
             @  6,6 say 'A nyn… p“ipoj…m v”echny v≈ty, kter◊ jsi  pr¡v≈ pracn≈'
             @  8,6 say 'na‘ukal do sv◊ho SHARPIKA, k z¡kladn… datab¡zi AR.DBF'
             @ 10,6 say 'Je jich celkem '+str(ZAPIS,5)+'.'
             @ 12,6 say 'A⁄ budu hotov, d¡m Ti v≈d≈t klasickou zpr¡vou !!!' 
             use &DISKAR
             append from &PARD
             use
            else 
             erase
             @ 12,6 say 'To tedy byl zase jednou vŸkon !!!!   F U J .'
             use
            endif
           @ 16,37 say 'Zm¡√kni cokoliv .........'
           @ 0,0         
           set console off
           wait
           set console on
      case VOLBA = '6'
           erase
           store '&MAR'+'.mem' to MAR
           delete file &MAR
           delete file &PARP
            if file ('&PARV')
            delete file &PARV
            endif 
            if file ('&PARD')
            delete file &PARD
            endif
            release all

           *disp memo
           set talk on
           set colon on
           set raw on
           *cancel
           quit
   endcase
enddo
