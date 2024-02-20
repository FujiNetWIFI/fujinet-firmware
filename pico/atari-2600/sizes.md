    
#                              Cart Information
#                              ---- -----------



              Info about cart sizes and bankswitching methods



                               By Kevin Horton
                              khorton@iquest.net


V6.00
Last modified on 4/18/97
(614 images)

Copyright 1997 K Horton, all rights reserved!  You may copy and distribute
this file as long as it remains intact and un-modified.


This text has been modified to be machine-readable by Bankzilla's database
generator.  This includes the '####Data Start####' and '####Data End####'
flags, as well as including an 'overflow' category and including controller
type.


#                           How Bankswitching Works
#                           --- ------------- -----

Bankswitching allows game programmers to include more data into a cartridge,
therefore making (hopefully) a better game with more graphics/ levels.

Bankswitching works under similar principals in all cases.  Basically, by
reading a certain location in ROM switches banks.  


(this is the F8-style of bankswitching)

Bank #1                               Bank #2
--------------------------------------------------------------
1000 JSR $1800 (do subroutine)        .
1003 (program continues)              1200 _subroutine goes here_
.                                     1209 RTS
.                                     .
1800 LDA $1FF9 (switch to bank 2)     1802 (rest of program)
1803 NOP                              1803 JSR $1200
1804 NOP                              .       
1805 NOP                              .       
1806 NOP                              1806 LDA $1FF8  (Switch back to bank 1)        
1807 NOP                              .
1808 NOP                              .
1809 RTS   (We're done w/ routine)    1809 (rest of program)                          

OK, we start out in bank #1 and we want to run a subroutine in bank #2.

What happens is this- the processor starts at 1000h in bank #1.  We call
our subroutine from here.  1800h:  We do a read to change us to bank #2.
Remember that when we change banks, we are basically doing a ROM swap.
(You can think of bankswitching as 'hot-swapping' ROMs)  Now that we're
in bank #2, the processor sees that JSR to $1200, which is the subroutine
that we wanted to execute.  We execute the subroutine and exit it with an
RTS.  This brings us back to 1806h.  We then do another read to select bank
#1.  After this instruction finishes, the processor is now in bank #1, with
the program counter pointing to 1809, which is an RTS which will take us
back to 1003 and let us continue on with our program.


                             Extra RAM in Carts
                             ----- --- -- -----

Some carts have extra RAM; There are three known formats for this:

Atari's 'Super Chip' is nothing more than a 128-byte RAM chip that maps
itsself in the first 256 bytes of cart memory.  (1000-10FFh)  
The first 128 bytes is the write port, while the second 128 bytes is the
read port.  This is needed, because there is no R/W line to the cart.

CBS  RAM Plus (RAM+)  This maps in 256 bytes of RAM in the first 512 bytes
of the cart; 1000-11FF.  The lower 256 addresses are the write port, while
the upper 256 addresses are the read port.  To store a byte and retrieve it:

LDA #$69  ; byte to store
STA $1000 ; store it
.
.         ; rest of program goes here
.
LDA $1100 ; read it back
.         ; acc=$69, which is what we stored here earlier.


M-network (AFAIK it has no name)

OK, the RAM setup in these carts is very complex.  There is a total of 2K
of RAM broken up into 2 1K pieces.  One 1K piece goes into 1000-17FF
if the bankswitch is set to $1FE7.  The other is broken up into 4 256-byte
parts.

You select which part to use by issuing a fake read to 1FE8-1FEB.  The
RAM is then available for use by all banks at 1800-19FF.  

Similar to other schemes, 1800-18FF is write while 1900-19FF is read.
Low RAM uses 1000-13FF for write and 1400-17FF for read.


Note that the 256-byte banks and the large 1K bank are seperate entities.
The M-Network carts are about as complex as it gets.


                   Descriptions of the Various Bankswitch Modes   
                   --------------------------------------------

2K: 
 
 -These carts are not bankswitched, however the data repeats twice in the
  4K address space.  You'll need to manually double-up these images to 4K
  if you want to put these in say, a 4K cart.

4K:
 
 -These images are not bankswitched.

6K:

 -AR: The Arcadia (aka Starpath) Supercharger uses 6K of RAM to store the 
  games loaded from tape. 

8K:
 
 -F8: This is the 'standard' method to implement 8K carts.  There are two
  addresses which select between two unique 4K sections.  They are 1FF8
  and 1FF9.  Any access to either one of these locations switches banks.
  Accessing 1FF8 switches in the first 4K, and accessing 1FF9 switches in
  the last 4K.  Note that you can only access one 4K at a time!
 
 -FE: Used only on two carts (Robot Tank and Decathlon).  You select banks
  via accesses to the stack.  You set the stack pointer to FF, and then a
  JSR switches banks one way, while RTS switches you back to the original
  bank (both banks are 4K).  This allows the programmers to perform 
  'automatic' bankswitching.  All the subroutines are in one bank, while
  all the game code is in another.  When you perform a JSR; you switch banks
  to the bank containg the subroutines.  Upon encoutering an RTS, the bank
  is switched back to the original calling bank.  Pretty spiffy!

 -E0: Parker Brothers was the main user of this method.  This cart is
  segmented into 4 1K segments.  Each segment can point to one 1K slice of 
  the ROM image.  You select the desired 1K slice by accessing 1FE0 to 1FE7
  for the first 1K (1FE0 selects slice 0, 1FE1 selects slice 1, etc).
  1FE8 to 1FEF selects the slice for the second 1K, and 1FF0 to 1FF8 selects
  the slice for the third 1K.  The last 1K always points to the last 1K
  of the ROM image so that the cart always starts up in the exact same place.

 -3F: Tigervision was the only user of this intresting method.  This works
  in a similar fashion to the above method; however, there are only 4 2K
  segments instead of 4 1K ones, and the ROM image is broken up into 4 2K
  slices.  As before, the last 2K always points to the last 2K of the image.
  You select the desired bank by performing an STA $3F instruction.  The
  accumulator holds the desired bank number (0-3; only the lower two bits
  are used).  Any STA in the $00-$3F range will change banks.  This appears to
  interfere with the TIA addresses, which it does; however you just use
  $40 to $7F instead! :-)  $3F does not have a corresponding TIA register, so
  writing here has no effect other than switching banks.  Very clever;
  especially since you can implement this with only one chip! (a 74LS173)

12K:

 -FA: Used only by CBS.  Similar to F8, except you have three 4K banks 
  instead of two.  You select the desired bank via 1FF8, 1FF9, and 1FFA.
  These carts also have 256 bytes of RAM mapped in at 1000-11FF.  1000-10FF
  is the write port while 1100-11FF is the read port.

16K:

 -F6: The 'standard' method for implementing 16K of data.  It is identical
  to the F8 method above, except there are 4 4K banks.  You select which
  4K bank by accessing 1FF6, 1FF7, 1FF8, and 1FF9.

 -E7: Only M-Network used this scheme.  This has to be the most complex
  method used in any cart! :-)  It allows for the capability of 2K of RAM;
  although it doesn't have to be used (in fact, only one cart used it-
  Burgertime).  This is similar to the 3F type with a few changes.  There are
  now 8 2K banks, instead of 4.  The last 2K in the cart always points to
  the last 2K of the ROM image, while the first 2K is selectable.  You
  access 1FE0 to 1FE6 to select which 2K bank. Note that you cannot select 
  the last 2K of the ROM image into the lower 2K of the cart!  Accessing
  1FE7 selects 1K of RAM at 1000-17FF instead of ROM!  The 2K of RAM is 
  broken up into two 1K sections.  One 1K section is mapped in at 1000-17FF
  if 1FE7 has been accessed.  1000-13FF is the write port, while 1400-17FF
  is the read port.  The second 1K of RAM appears at 1800-19FF.  1800-18FF
  is the write port while 1900-19FF is the read port.  You select which
  256 byte block appears here by accessing 1FF8 to 1FFB.
  
32K

 -F4: The 'standard' method for implementing 32K.  Only one cart is known
  to use it- Fatal Run.  Like the F6 method, however there are 8 4K
  banks instead of 4.  You use 1FF4 to 1FFB to select the desired bank.

64K

 -F0: Only used one cart, AFAIK.  (the 'Megaboy' cart from Dynacom)  It
  has 16 4K banks.  Accessing 1FF0 will increment the current bank.  The
  program uses location 1FEC to tell it which bank it's in.  There's a
  little loop at 1FE0 that checks this location against the accumulator,
  and if they're equal it does an RTS.  Otherwise it does an STA 1FF0
  and repeats the loop.  


  /-----------------------------------------------------------------------\   
  |                                    KEY                                |
  |                                    ---                                |
  |  Name     -  Game Name                                                |
  |  Part #   -  Part Number of the actual cart                           |
  |  RA       -  Rarity, according to VGR's guide and my observations     |
  |  SZ       -  Size of the ROM image in K                               |
  |  SC       -  If the cart has a Special Chip                           |
  |  BS       -  Bankswitch method used (see below)                       |
  |  IM       -  'X'ed if I have the image                                |
  |  SP       -  Special Attribute (See the end of a section for details) |
  |  CT       -  Controller type (See below)                              |
  |  Filename -  The filename of the ROM image                            |
  |                                                                       |
  |                              Bankswitch Types:                        |
  |                              -----------------                        |
  |                      (See above for full descriptions)                |
  |                                                                       |
  |     - (nothing);  Not bankswitched (2K and 4K only)                   |
  |  F8 - 'Standard' 8K; uses 1FF8 and 1FF9                               |
  |  F6 - 'Standard' 16K; uses 1FF6 to 1FF9                               |
  |  F4 - 'Standard' 32K; uses 1FF4 to 1FFB                               |
  |  F0 - Megaboy 64K; uses 1FF0 to increment bank #                      |
  |  SC - Superchip; 128 bytes of RAM @ 1000-10FF (i.e. F8+SC, F4+SC)     |
  |  FA - 'RAM+' 12K; uses 1FF8 to 1FFA; 256 bytes of RAM @ 1000 to 11FF  |
  |  FE - 'Activision' 8K; uses 01FE and 01FF to determine bank           |
  |  E0 - 'Parker Brothers' 8K; uses 1FE0 to 1FF7                         |
  |  E7 - 'M-Network' 16K; Uses 1FE0 to 1FE7 and 2K of RAM at 1800-19FF   |
  |  3F - 'Tigervision' 8K; Uses STA $3F to determine bank #              |
  |  AR - 'Arcadia' 6K; Used on the Supercharger                          |
  |  ?? - Unknown at this time                                            |
  |                                                                       |
  |                             Controller Types:                         |
  |                             -----------------                         |
  |                                                                       |
  |     - (nothing); Unknown at this time                                 |
  |   J - Joystick                                                        |
  |   P - Paddles                                                         |
  |   K - Keypad                                                          |
  |  JK - Joystick and keypad (Star Raiders)                              |
  |   D - Driving Controllers                                             |
  |   B - Joystick plus Booster Grip (Omega Race)                         |
  |   T - Track & Field controller                                        |
  |   O - Other                                                           |
  |   L - Light Gun                                                       |
  |                                                                       |
  |                                                                       |
  |        I have added two new categories to the rarity rating:          |
  |                                                                       |
  |  PR - This was only available as a prototype                          |
  |  DM - This image is only a demo, and not really a game                |
  |                                                                       |
  \-----------------------------------------------------------------------/




#               Hot Wants (that I know I probably won't get :-)
#               -----------------------------------------------


Tempest                          (Atari)
Good Luck, Charlie Brown         (Atari)
Miss Piggy's Wedding (exists?)   (Atari)
Wizard                           (Atari)
BMX Airmaster (atari ver)        (Atari)
White Water Madness (exists?)    (Atari)
Rodeo                            (Atari)
Rabbit Transit                   (Atari)
Nightmare Manor                  (Atari)
Pink Panther                     (Probe 2000)
The Impossible Game              (Telesys)
Ewoks Adventure                  (Parker Bros)
Thwoker                          (Activision)
Out of Control                   (Avalon Hill)
Berenstein Bears                 (Coleco)
Video Life                       (Commavid)
Aerial Ace (exist?)              (Imagic)
Lady in Wading                   (Playaround)
Snowplow                         (Sunrise)
Noah and the Ark                 (Sunrise)
Meltdown (exist?)                (20th cent.)
Tomarc the Barabrrian (exist?)   (Xonox)
Motocross Racer (exist?)         (Xonox)



Anything by Action Hi-tech





####Data Start####


***********************************
*            Atari                *
***********************************

[If SC is marked, cart uses a 'Super Chip'; aka CO20231]

         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------
                                                          
Combat                     CX2601       C   2           X       J  COMBAT
Air-Sea Battle             CX2602       U   2           X       J  AIR_SEA
Star Ship                  CX2603       R   2           X          STARSHIP
Space War                  CX2604       U   2           X          SPACEWAR
Outlaw                     CX2605       U   2           X       J  OUTLAW
Slot Racers                CX2606       U   2           X          SLOTRACE
Canyon Bomber              CX2607       U   2           X          CANYONB
Super Breakout             CX2608       U   4           X       P  SUPERB
Defender                   CX2609       C   4           X       J  DEFENDER
Warlords                   CX2610       U   4           X       P  WARLORDS
Indy 500                   CX2611       U   2           X       D  INDY_500
Street Racer               CX2612       U   2           X          STRTRACE
Adventure                  CX2613       C   4           X       J  ADVNTURE
Steeple Chase              CX2614      NR   2           X       P  STEPLCHS
Demons to Diamonds         CX2615       U   4           X          DEMONDIM
Hot Rox                    CX2615      NR   4           X          DEMONDIM
Pele's Soccer              CX2616       C   4           X          PELE
Backgammon                 CX2617       U   4           X          BACKGAM
3D- Tic-Tac-Toe            CX2618       U   2           X       J  3D_TIC
Stellar Track              CX2619      NR   4           X          STELRTRK
BASIC Programming          CX2620       R   4           X       K  BASIC
Video Olympics             CX2621       C   2           X       P  VID_OLYM
Breakout                   CX2622       C   2           X       P  BREAKOUT
Homerun                    CX2623       C   2           X          HOMERUN
Basketball                 CX2624       C   2           X          BASKETBL
Football                   CX2625       C   2           X          FOOTBALL
Minature Golf              CX2626       U   2           X          MIN_GOLF
Human Cannonball           CX2627       U   2           X          HUMAN_CB
Bowling                    CX2628       C   2           X       J  BOWLING
Sky Diver                  CX2629       U   2           X          SKYDIVER
Circus Atari               CX2630       C   4           X       P  CIRCATRI
Superman                   CX2631       C   4           X       J  SUPRMAN1
Space Invaders             CX2632       C   4           X       J  SPCINVAD
Night Driver               CX2633       C   2           X          NIGHTDRV
Golf                       CX2634       C   2           X          GOLF
Maze Craze                 CX2635       U   4           X       J  MAZECRZ
Video Checkers             CX2636       R   4           X       J  CHECKERS
Dodge 'Em                  CX2637       U   4           X          DODGE_EM
Missile Command            CX2638       C   4           X       J  MISSCOMM
Othello                    CX2639       R   2           X          OTHELLO
Realsports Baseball        CX2640       U   8      F8   X          RS_BASEB
Surround                   CX2641       C   2           X       J  SURROUND
A Game of Concentration    CX2642       C   2           X          CONCENTR
Code Breaker               CX2643       U   2           X          CODEBRK
Flag Capture               CX2644       U   2           X          FLAGCAP
Video Chess                CX2645       U   4           X          VIDCHESS
Pac-Man                    CX2646       C   4           X       J  PACMAN
Submarine Commander        CX2647      PR   4           X       J  SUBCOMDR
Video Pinball              CX2648       U   4           X       J  VIDPIN
Asteroids                  CX2649       C   8      F8   X       J  ASTEROID
Berzerk                    CX2650       C   4           X       J  BERZERK
Blackjack                  CX2651       R   2           X          BLACK_J
Casino                     CX2652       U   4           X          CASINO
Slot Machine               CX2653       R   2           X          SLOTMACH
Haunted House              CX2654       C   4           X       J  HAUNTHSE
Yar's Revenge              CX2655       C   4           X       J  YAR_REV
Swordquest Earthworld      CX2656       C   8      F8   X       J  SQ_EARTH
Swordquest Fireworld       CX2657       C   8      F8   X       J  SQ_FIRE
Math Gran Prix             CX2658       C   4           X       J  MATH_GPX
Raiders of the Lost Ark    CX2659       C   8      F8   X       J  RAIDERS
Star Raiders               CX2660       U   8      F8   X      JK  STARRAID
Basic Math                 CX2661       C   2           X       J  BASMATH
Hangman                    CX2662       U   4           X       J  HANGMAN
Road Runner                CX2663      ER  16      F6   X       J  ROADRUNR
Brain Games                CX2664       U   2           X          BRAINGMS
Frog Pond                  CX2665      PR   8      F8   X       J  FROGPOND
Realsports Volleyball      CX2666       U   4           X          RS_VOLLY
Realsports Soccer          CX2667       U   8      F8   X          RSSOCCER
Realsports Football        CX2668       C   8      F8   X          RS_FOOTB
Vanguard                   CX2669       C   8      F8   X          VANGUARD
Atari Video Cube           CX2670       R   4           X       J  VIDCUBE
Swordquest Waterworld      CX2671      UR   8      F8   X       J  SQ_WATER
Swordquest Airworld        CX2672      NR  ??  ---No Known Copies Exist---
Phoenix                    CX2673       C   8      F8   X       J  PHOENIX
E.T. The Extra-Terrestrial CX2674       C   8      F8   X       J  E_T
Ms. Pac-Man                CX2675       C   8      F8   X       J  MSPACMAN
Centipede                  CX2676       C   8      F8   X       J  CENTIPED
Dig Dug                    CX2677       U  16   X  F6   X       J  DIGDUG
Dukes of Hazzard           CX2678      PR  16      F6   X       J  DUKES
Realsports Basketball      CX2679      NR  ??  ---No Known Copies Exist---
Realsports Tennis          CX2680       U   8      F8   X       J  RSTENNIS
Battlezone                 CX2681       U   8      F8   X       J  BATLZONE
Krull                      CX2682       R   8      F8   X       J  KRULL
Crazy Climber              CX2683      ER   8      F8   X       J  CRAZCLMB
Galaxian                   CX2684       U   8      F8   X       J  GALAXIAN
Gravitar                   CX2685       U   8      F8   X       J  GRAVITAR
Quadrun                    CX2686      ER   8      F8   X       J  QUADRUN
Tempest                    CX2687      PR  ??  ---A Few Proto's Exist---
Junglehunt                 CX2688       U   8      F8   X       J  JNGLHUNT
Kangaroo                   CX2689       U   8      F8   X       J  KANGAROO
Pengo                      CX2690      ER   8      F8   X       J  PENGO
Joust                      CX2691       C   8      F8   X       J  JOUST
Moon Patrol                CX2692       U   8      F8   X       J  MOONPTRL
Food Fight                 CX2693      NR  ??  ---No Known Copies Exist---
Pole Position              CX2694       C   8      F8   X       J  POLEPSN
Xevious                    CX2695      PR   8      F8   X       J  XEVIOUS
Asterix                    CX2696      ER   8      F8   X       J  ASTERPAL
Mario Bros.                CX2697       U   8      F8   X       J  MARIOBRO
Rubik's Cube               CX2698      ER   4           X       J  RUBIKS
Taz                        CX2699       R   8      F8   X       J  TAZ
Oscar's Trash Race         CX26101      R   8      F8   X       K  OSCAR
Cookie Monster Crunch      CX26102      R   8      F8   X       K  COOKMONS
Alpha-Beam with Ernie      CX26103      R   8      F8   X       K  ALPHBEAM
Big Bird's Egg Catch       CX26104      R   8      F8   X       K  EGGCATCH
3-D Asteroids              CX26105     NR  ??
Grover's Music Maker       CX26106     PR   8      F8   X          GROVER
Snow White                 CX26107     NR  ??
Donald Duck's Speedboat    CX26108     PR   8      F8   X       J  DDUCKSBT
Sourcerer's Apprentice     CX26109      R   8      F8   X       J  SORCAPRN
Crystal Castles            CX26110      U  16   X  F6   X       J  XTALCAST
Snoopy and the Red Barron  CX26111      R   8      F8   X       J  SNOOPY
Good Luck; Charlie Brown   CX26112     PR  ??  ---One Proto Exists---
Miss Piggie's Wedding      CX26113     UR  ??
Pigs in Space              CX26114     ER   8      F8   X       J  PIGSPACE
Dumbo's Flying Circus      CX26115     PR   8      F8   X       J  DUMBO_N
Galaga                     CX26116     NR  ??
Obelix                     CX26117     ER   8      F8   X       J  OBELIX
Millipede                  CX26118      R  16   X  F6   X       J  MILLIPED
Saboteur                   CX26119     PR   8      F8   X       J  SABOTEUR
Star Gate                  CX26120      U   8   X  F8   X       J  STARGATE
Defender ][                CX26120      R   8   X  F8   X       J  DEFENDR2
Zookeeper                  CX26121     NR  ??
Sinistar                   CX26122     PR   8      F8   X       J  SINISTAR
Jr. Pac-Man                CX26123      U  16   X  F6   X       J  JRPACMAN
Choplifter                 CX26124     NR  ??
Track & Field              CX26125      R  16      F6   X       T  TRACK
Elevator Action            CX26126     NR  ??
Gremlins                   CX26127     ER   8      F8   X       J  GREMLINS
Boing                      CX26128     NR  ??
Midnight Magic             CX26129      R  16      F6   X       J  MIDNIGHT
Honker Bonker              CX26130     NR  ??
Monstercise                CX26131     PR   8      F8   X          MONS
Garfield                   CX26132     NR  ??
The A-Team                 CX26133     PR   8      F8   X       J  ATEAM
The Last Starfighter       CX26134     NR  ??
Star Raiders ][            CX26134     NR  ??
Realsports Boxing          CX26135      U  16      F6   X       J  RSBOXING
Solaris                    CX26136      U  16      F6   X       J  SOLARIS
Peek-A-Boo                 CX26137     PR   4           X          PEEKABOO
Super Soccer               CX26138     NR  ??
Crossbow                   CX26139      U  16      F6   X       J  CROSSBOW
Desert Falcon              CX26140      C  16   X  F6   X       J  DSRTFALC
Motor Psycho               CX26141     NR  ??   
Crack'ed                   CX26142         ??
Donkey Kong                CX26143      U   4           X       J  DK
Donkey Kong Jr.            CX26144      R   8      F8   X       J  DKJR
Venture                    CX26145      R   4           X       J  VENTURE
Mousetrap                  CX26146      R   4           X       J  MOUSETRP
Frogger                    CX26147     NR   4           X       J  FROGGER
Turbo                      CX26148     NR  ??
Zaxxon                     CX26149     NR  ?? 
Q*Bert                     CX26150      R   4           X       J  QBERT_PB
Dark Chambers              CX26151      U  16   X  F6   X  X3   J  DARKC
Super Baseball             CX26152      U  16      F6   X       J  SUPBBALL
Super Football             CX26154      U  16   X  F6   X       J  SPRFOOTB
Sprintmaster               CX26155      R  16   X  F6   X       J  SPRNMAST
Combat II (Wizard?)        CX26156     NR  ??     
                           CX26157         ??       
Surround II                CX26158     NR  ??                  
Double Dunk                CX26159      R  16      F6   X       J  DOUBDUNK
                           CX26160         ??       
                           CX26161         ??       
Fatal Run                  CX26162     UR  32   X  F4   X       J  FATALRUN
32-in-1                    CX26163     ER  64  32 banks of 2K  --  32IN1
                           CX26164         ??       
Jinks                      CX26165     NR  ??
                           CX26166         ??       
Street Fight               CX26167         ??
Off the Wall               CX26168     ER  16   X  F6   X       J  OFTHWALL
Shooting Arcade            CX26169     PR  16   X  F6   X       L  SHOOTING
Secret Quest               CX26170      R  16   X  F6   X       J  SECRETQ
Motorodeo                  CX26171     UR  16      F6   X       J  MOTOR
Xenophobe                  CX26172     ER  16      F6   X       J  XENOPHOB
                           CX26173         ??       
                           CX26174         ??       
                           CX26175         ??       
Radar Lock                 CX26176      R  16   X  F6   X       J  RADARLOK
Ikari Warriors             CX26177      R  16      F6   X       J  IKARIWAR
Save Mary!                 CX26178     PR  16   X  F6   X       J  SAVEMARY
                           CX26179         ??       
                           CX26180         ??       
                           CX26181         ??       
                           CX26182         ??       
Sentinel                   CX26183      R  16      F6   X       L  SENTINEL
White Water Madness        CX26184     UR  ??
                           CX26185         ??       
                           CX26186         ??       
                           CX26187         ??       
                           CX26188         ??       
                           CX26189         ??       
BMX Airmaster              CX26190     ER  ??       
                           CX26191         ??                  
KLAX                       CX26192     ER  16   X  F6   X  X2   J  KLAXNTSC 
                                                             

Aquaventure                CX26???     PR   8      F8   X       J  AQUAVENT
Bionic Breakthrough        CX26???     PR   8      F8   X       O  MINDLINK
Bugs Bunny                 CX26???     PR   8      F8   X       J  BUGSBUN
Coke Wins!                 CX26???     UR   4           X       J  COKEWINS
Holy Moley                 CX26???     PR   8      F8   X          HOLEMOLE
Polo                       CX26???     PR   2           X       J  POLO
Rodeo                      CX26???     PR  ??             
Rabbit Transit             CX26???     PR  ??
Standalone Test Tape       MAO17600    DM   2           X      --  MAO17600
Nightmare Manor            CX26???     PR  ??
Super Stunt Cycle                      PR   2           X       J  STUNT-1
Dukes of Hazzard (not CX2678)          PR   2           X       J  STUNT-2

X2: Special Best Prototype NTSC version.

X3: Doesn't like my test cart; have to disable SC for it to start.  I
    can then re-enable the SC and it'll work.  Also, the cart itsself
    doesn't work on my 7800 (or my test cart for that matter).


***********************************
*         Action Hi-Tech          *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------
                          
Crab Control               605077      UR  ??
F-18 vs. Aliens            ????        UR  ??
Galaxy Invader             ????        UR  ??
Space Grid                 ????        UR  ??
Tank City                  ????        UR  ??
War Zone                   ????        UR  ??


***********************************
*            Activision           *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Dragster                   AG-001       U   2           X       J  DRAGSTER
Boxing                     AG-002       U   2           X       J  BOXING
Checkers                   AG-003      ER   2           X       J  CHECKERA
Fishing Derby              AG-004       U   2           X       J  FISHDRBY
Skiing                     AG-005       U   2           X       J  SKIING
Bridge                     AX-006       U   4           X       J  BRIDGE
Tennis                     AG-007       C   2           X       J  TENNIS
Laser Blast                AG-008       C   2           X       J  LASRBLST
Freeway                    AG-009       U   2           X       J  FREEWAY
Kaboom                     AG-010       U   2           X       P  KABOOM
Stampede                   AG-011       U   2           X       J  STAMPEDE
Ice hockey                 AX-012       U   4           X       J  ICEHOCKY
Barnstroming               AX-013       U   4           X       J  BARNSTRM
Gran Prix                  AX-014       U   4           X       J  GRANDPRX
Chopper Command            AX-015       U   4           X       J  CHOPRCMD
Starmaster                 AX-016       U   4           X       J  STARMAST
Megamania                  AX-017       U   4           X       J  MEGAMAN
Pitfall                    AX-018       C   4           X       J  PITFALL
Sky Jinks                  AG-019       R   2           X       J  SKYJINKS
River Raid                 AX-020       U   4           X       J  RIVERAID
Spider Fighter             AX-021       U   4           X       J  SPIDRFTR
Seaquest                   AX-022       R   4           X       J  SEAQUEST
Oink!                      AX-023       R   4           X       J  OINK
Dolphin                    AX-024       R   4           X       J  DOLPHIN
Keystone Kapers            AX-025       U   4           X       J  KEYSTONE
Enduro                     AX-026       U   4           X       J  ENDURO_A
Plaque Attack              AX-027       R   4           X       J  PLAQATTK
Robot Tank                 AZ-028       R   8      F8   X  X2   J  ROBO_FIX
Crackpots                  AX-029       R   4           X       J  CRACKPOT
Decathlon                  AZ-030       R   8      FE   X       J  DECATHLN
Frostbite                  AX-031       R   4           X       J  FROSTBIT
Pressure Cooker            AZ-032       R   8      F8   X       J  PRESSURE
Space Shuttle              AZ-033       R   8      F8   X       J  SPCSHUTL
Private Eye                AG-034-04    R   8      F8   X       J  PRIVEYE
Pitfall ][: Lost Caverns   AB-035-04    R   8      F8   X  X1   J  PITFALL2
H.E.R.O.                   AZ-036-04    R   8      F8   X       J  HERO
Beamrider                  AZ-037-04    R   8      F8   X       J  BEAMRIDE
Cosmic Commuter            AG-038       R   4           X       J  CSMCOMTR
Kung-Fu Master             AX-039       R   8      F8   X       J  KUNG_FU
Contenders                 AK-041     ---Never Released by Activision---
Commando                   AK-043       R  16      F6   X       J  COMMANDO
Fighter Pilot              AK-046      UR  16      F6   X       J  FIGHTERP
River Raid ][              AK-048-04   ER  16      F6   X       J  RIVRAID2
Rampage                    AK-049       R  16      F6   X       J  RAMPAGE
Double Dragon              AK-050-04    R  16      F6   X       J  DBLDRAGN
Ghostbusters               AZ-108-04    R   8      F8   X       J  GHOSTBST
Ghostbusters ][            A?-???      NR  ?? ---See Salu---
Dreadnaught Factor         A?-???      NR  ?? 
Wing War                   A?-???      NR  ?? ---See Imagic---
Thwocker                   A?-???      PR  ?? ---Proto Exists---
Zenji                      A?-???      NR  ??


X1: Uses Activision's version of the 'Super Chip'  No extra RAM this time,
    however it does have extra ROM!  Does three-channel sound, and even
    includes several random # generators.


X2: This used to be an FE cart, but has been fixed to run as an F8.

***********************************
*     Absolute Entertainment      *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Title Match Pro Wresting   AG-041       R   8      F8   X       J  PROWREST
Skateboardin'              AG-042       R   8      F8   X       J  SKATEBRD
Pete Rose Baseball         AK-045       R  16      F6   X       J  PETEROSE
Tomcat F-14 Simulator      AK-046      ER  16      F6   X  X1   J  FIGHTERP  
My Golf                    A?-???      ER   8      F8   X       J  MYGOLF

X1:  This is identical to the Activision one.  I read both in and compared;
they are byte-for-byte identical. 

***********************************
*       American Videogame        *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Tax Avoiders               ????         R   8      F8   X       J  TAXAVOID
                                                                 

***********************************
*             Amiga               *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------
                                       
Mogul Maniac               3120        ER   4           X       J  MOGULMAN
Surf's Up                  3125        PR   8      F8   X       J  SURF_FIX
Off your Rocker            3130        PR   4           X       J  OFFROCKR


***********************************
*         Answer Software         *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Malagai                    ASC1001     UR   4           X       J  MALAGAI
Gauntlet                   ASC1002     UR   4           X       J  GAUNTLET
Confrontation              ASC2001     UR  ??
Personal Game Programmer   PGP-1       UR  XX      ---Hardware---


***********************************
*              Artic              *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Space Robot                SM8001      UR   4           X       J  SPCROBOT
Astrowar                   SM8002      UR   4           X       J  ASTROWAR


***********************************
*             Apollo              *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Skeet Shoot                AP 1001      R   2           X          SKEETSHT
Spacechase                 AP 2001      U   4           X          SPACHASE
Space Cavern               AP 2002      U   4           X          SPACECAV
Racquetball                AP 2003      U   4           X          RACQUETB
Lost Luggage               AP 2004      U   4           X       J  LOSTLUGG
Lochjaw                    AP 2005     ER   4           X       J  LOCHJAW
Shark Attack               AP 2005      U   4           X       J  SHARKATK
Infiltrate                 AP 2006      U   4           X       J  INFILTRT
Kyphus                     AP 2007      R  ??
Guardian                   AP 2008      R   4           X          GUARDIAN
Final Approach             AP 2009      R   4           X       J  FINLAPCH
Wabbit                     AP 2010      R   4           X       J  WABBIT
Pompeii                    AP 2011      R  ??
Squoosh                    AP 2012     PR  ??


***********************************
*          Avalon Hill            *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Death Trap                 50010       UR   4           X       J  DETHTRAP
London Blitz               50020       ER   4           X       J  LONDBLTZ
Wall Ball                  50030       ER   4           X       J  WALLBALL
Shuttle Orbiter            50040       UR   4           X       J  SHTLORBT
Out of Control             50050       UR  ??           Need!


***********************************
*            Bit Corp.            *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Sea Monster                PG201        R   4           X          SEAMNSTR
Space Tunnel               PG202        R   4           X          SPACT_TW
Phantom Tank               PG203        R   4           X          PHANTOMT
Open Sesame                PG204        R   4           X          OPENSESM
Dancing Plate              PG205        R   4           X          DANCPLAT
Bobby is Going Home        PG206        R   4           X       J  BOBBY
Mission 3000 AD            PG207        R   4           X          MISN3000
Snail Against Squirrel     PG208        R   4           X          SNALSQRL
Mr. Postman                PG209        R   4           X          MRPOSTMN
Superman      (CCE)        ????        ??   4           X          SUPERCCE

***********************************
*           Bob Colbert           *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Okie Dokie  (Lights Out)   ????        --   2           X       J  OKIEDOKE


***********************************
*              Bomb               *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Assault                    CA281       ER   4           X       J  ASSAULT
Great Escape               CA282       ER   4           X       J  GRESCAPE
Z-Tack                     CA283       ER   4           X       J  Z_TACK
Wall Defender              CA285       ER   4           X       J  WALLDFND


***********************************
*        CBS Electronics          *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------
                                                        
Wizard of Wor              M8774        R   4           X       J  WIZRDWOR
Gorf                       M8793        U   4           X       J  GORF
Blueprint                  4L-2486      U   8      F8   X       J  BLUEPRNT
Solar Fox                  4L-2487      U   8      F8   X       J  SOLARFOX
Tunnel Runner              4L-2520      R  12      FA   X       J  TUNLRUNR
Omega Race                 4L-2737      U  12      FA   X       B  OMEGARAC
Mountain King              4L-2738      R  12      FA   X       J  MTNKING
Wings                      ????        NR  ??      ??

'SC' in this case refers to RAM+

***********************************
*            Coleco               *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Donkey Kong                2451         C   4           X       J  DK
Zaxxon                     2454         U   8      F8   X       J  ZAXXON
Venture                    2457         C   4           X       J  VENTURE
Mouse Trap                 2459         U   4           X       J  MOUSETRP
Lady Bug                   2463        UR  ??
Cosmic Avenger             2464        UR  ??
Smurf: RIGC                2465         U   8      F8   X       J  SMURFRES
Carnival                   2468         U   4           X       J  CARNIVAL
Smurfs Save the Day        2511        UR   8      F8   X       J  SMRFSAVE
Donkey Kong Jr.            2653         R   8      F8   X       J  DKJR
Mr. Do!                    2656         R   8      F8   X       J  MRDO
Berinstein Bears           2658        UR  ??
Time Pilot                 2663         R   8      F8   X       J  TIMEPLT
Front Line                 2665         R   8      F8   X       J  FRNTLINE
Roc 'N Rope                2667         R   8      F8   X       J  ROCNROPE
                                                                 


*addendum*
Got Mr. Do! to read out, and Time Pilot seems to have read out correctly,
yet it doesn't play on the emu.  I'm going to pull the EPROM off the board
and read it directly!
*end*

*addendum2*
Pulled the EPROM and copied it.  Just as I suspected, it did indeed read out
correctly.  Guess the emu isn't as good as the Real Thing. :-)  I'll try it
out on the real hardware RSN.
*end*

*addendum3*
The saga continues.  I tried the ROM image of Time Pilot out on the Real 
Thing, and it behaved the exact same way that it did on the emu.  It appears
that the RC delay in the cart is required so it doesn't switch banks
immediately.  I hope I can fix it so it can work as a normal F8 cart.
*end*

*addendum4*
Yes!  I got Time Pilot to work!  This is intresting.  The bank is *only*
flipped in two parts of the cart- once at the beginning of bank0 and once
at the beginning of bank1.  The tip-off was bank #0 had a BIT $1FF8 
instruction and bank #1 had a BIT $1FF9 instruction!  This of course will
*not* flip banks!!!  I changed the 4K blocks around, ran it on the emu, and
it worked perfectly!  
*end*

*addendum5*
Finally figured out what was wrong with Smurfs Save the Day.  The places
in the code that switch banks was right on top of each other.  (the 
STA $1FFx instructions were at the same addresses in diffrent banks)
As a result, there were only STA $1FF8 instructions rather than both
STA $1FF8 and STA $1FF9 instructions.  Fixing these resulted in a working
ROM image!!!!!!  Now all I need is Berinstein Bears to round out the
Coleco Collection.
*end*

***********************************
*           CommaVid              *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

MagiCard                   CM-001      UR  ??         ----Hardware----
Video Life                 CM-002      UR  ??
Cosmic Swarm               CM-003       R   2           X       J  COSMSWRM
Room of Doom               CM-004      ER   4           X       J  ROOMDOOM
Mines of Minos             CM-005      ER   4           X          MINEMNOS
Cakewalk                   CM-008      ER   4           X       J  CAKEWALK
Stronghold                 CM-009      ER   4           X          STRNGHLD


***********************************
*            Data Age             *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Encounter at L-5           DA 1001      U   4           X          ENCONTL5
Warplock                   DA 1002      C   4           X          WARPLOCK
SSSnake                    DA 1003      C   4           X          SSSNAKE
Airlock                    DA 1004      C   4           X          AIRLOCK
Bugs                       DA 1005      U   4           X       P  BUGS
Journey Escape             112-006      C   4           X       J  JRNYESCP
Rock 'n Roll Escape        112-006      R   4           X  X1   J  JRNYESCP
Bermuda Triangle           112-007      R   4           X       J  BERMDTRI
Frankenstein's Monster     112-008      R   4           X       J  FRANKMON
Secret Agent               ????        UR  ??


X1: This is the same as Journey Escape.


***********************************
*            DSD/Camelot          *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Tooth Protectors            ????       UR   8      E0   X  X1   J  TOOTHPRO


X1: Intresting!!!  This cart uses the Parker Bros. 8K bankswitch!!!  It's
the only non-PB cart to use this format.



***********************************
*             Dynacom             *
***********************************

         
         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Mega Boy                   ????        ??  64      F0   X       J  MEGABOY


This is a very intresting cart.  It's designed as an educational product!
It was only test-marketed in Brazil, and most of it is in Portugese. It
contains several different learning tools- Math, English, Games, and Music!
This cart goes with a hand-held 2600 called the 'Mega Boy'... it's similar
to a TV Boy except it can accept regular 2600 carts!  It runs on batteries
and transmits through the air to a TV in a similar fashion to a TV Boy.  All
in all a very cool device.



***********************************
*         Ed Fendermyer           *
***********************************

         
         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------
SoundX                     ????        ER   4           X       J  SOUNDX
EdTris                     ????        ER   4           X       J  EDTRIS


***********************************
*             Emag                *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

I Want My Mommy           GN-010       ER   4           X          IWANTMOM
Dishaster                 GN-020       ER   4           X       J  DISHASTR
Tanks But No Tanks        GN-030       ER   4           X          TANKSBUT
Cosmic Corridor           GN-040       ER   4           X          COSMCORR
Pizza Chef                GN-050       ER   4           X       J  PIZZA
Immies & Aggies           GN-060       ER   4           X          IMMIES
A Mysterious Thief        GN-070       ER  ??
Fire Spinner              GN-080       ER   4           X          FIRESPIN


***********************************
*             Epyx                *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------
                          
Summer Games               8056100250   R  16      F6   X       J  SUMMERGA
Winter Games               8056100251   R  16      F6   X       J  WINTERGA
California Games           8056100286   R  16      F6   X       J  CALIFGMS
Super Cycle                ????            ??


***********************************
*             Exus                *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------
                          
Video Jogger               ????        ??   4           X       O  VIDJOGGR
Video Reflex               ????        ??   4           X       O  VIDREFLX

***********************************
*       First Star Software       *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------
                          
Boing!                     ????        ER   4           X       J  BOING

***********************************
*             Froggo              *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Karate                     FG 1001      R   4           X       J  KARATE
Spiderdroid                FG 1002      R   4           X       J  SPIDROID
Task Force                 FG 1003      R   4           X       J  TASKFORC
Cruise Missile             FG 1007      R   4           X       J  CRUSMISL
Sea Hawk                   FG 1008      R   4           X       J  SEAHWK_F
Sea Hunt                   FG 1009      R   4           X       J  SEA_HUNT


***********************************
*            Funvision            *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------
                          
Ocean City Defender        ????        ??   4           X       J  OCEANCTY 
Spider Maze                ????        ??   4           X       J  SPDRMAZE


***********************************
*               HES               *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------
                          
Challenge                  ????        ??   8      F8   X       J  CHALLANG
My Golf                    535         ??   8      F8   X       J  MYGOLF
Pigs 'n Wolf               ????        ??  ??
Star Warrior               ????        ??  ??

Go for the Gold Pak (really two carts)
 -Winter Games                         ??  16      F6   X       J  WINTERG2
 -Summer Games                         ??  16      F6   X       J  SUMMERG2

****
Special menued multicarts:
****

Super Action Pak           223         ??  16      F6   X       J  SUPERACT
 -Pitfall
 -Grand Prix
 -Laser Blast
 -Barnstorming
Smash Hit Pak              498         ??  16      F6   X       J  SMASHHIT
 -Frogger
 -Stampede
 -Seaquest
 -Boxing
 -Skiing
Hot Action Pak             542         ??  16      F6   X       J  HOTPAK
 -Ghostbusters
 -Plaque Attack
 -Tennis
Rad Action Pak             559         ??  16      F6   X       J  RADACT
 -Kung-Fu Master
 -Frostbite
 -Freeway
Mega Fun Pak               ????        ??  ??      F6   X       J  MEGAPAK
 -Gorf
 -Planet Patrol
 -Pac-Man
 -Skeet Shoot
Sports Action Pak          ????        ??  16      F6   X       J  SPORTACT
 -Enduro
 -Ice hockey
 -Fishing Derby
 -Dragster
Super Hit Pak              ????        ??  16      F6   X       J  SUPERHIT
 -River Raid
 -Grand Prix
 -Fishing Derby
 -Jink
 -Checkers
2 Pak Special #1           ????        ??  16      F6   X       J  P0
 -Dungeon Master  -  Venture
 -Creature Strike -  Demon Attack
2 Pak Special #2           ????        ??  16      F6   X       J  P1
 -Star Warrior    -  Starwars: Empire Strikes Back                                  
 -Frogger     
2 Pak Special #3           ????        ??  16      F6   X       J  P2
 -Wall Defender 
 -Planet Patrol
2 Pak Special #4           ????        ??  16      F6   X       J  P3
 -Space Voyage    -  Starmaster
 -Fire Alert      -  Fire Fighter
2 Pak Special #5           ????        ??  ??   
 -Alien Force
 -Hoppy
2 Pak Special #6           ????        ??  ??
 -Cavern Blaster
 -City War
2 Pak Special #7           ????        ??  ??
 -Challenge
 -Surfing
2 Pak Special #8           ????        ??  ??
 -Dolphin
 -Pigs 'n Wolf
2 Pak Special #9           ????        ??  ??
 -Motocross
 -Boom Bang


Notes about menued carts:  These are very intresting!  They consist of
several games in seperate banks of a 16K F6 bankswitched ROM.  There's a
very slick looking menu that comes up displaying the co's logo (HES), and
to press the fire button.  After doing so, the user is given a choice of
what game to play.  The choices are actually written out onto the screen in
hi-res text!  You highlight the desired game and hit the button.  The tech
behind it is pretty simple, yet clever.  On startup, the bank is pointed to
the menu system's bank, and then is run just like any other F6 cart.  The
games are stored in seperate banks, or the upper 2K of a 4K block with the
lower 2K being the menuing program.  When the user selects a game, a small
'stub' of code is written to RAM then executed; this stub is usually 
something like this:

0080:   LDA $1FF8   ;change banks
0083:   JMP $1000   ;run game

So that when 0080 is called, the cart is switched to bank #2, and then the
game in said bank will be run.  Pretty nifty!  Note that since this is 
already a bankswitched game, 8K bankswitched games can be run in it, as well
as non-bankswitched games.  Check out the game linup on the 'Hot Action Pak'.
It is:

Ghost Busters  8K
Plaque Attack  4K
Tennis         2K

Total:         14K

That gives us the extra 2K for the menuing system.

On those '2 Pak Special' carts, they are still 16K, but almost half of this
goes to waste; they could've made '3 Pak Specials' to use up most of the
space no problem.  I still don't know why they didn't do this.  Examining the
ROM shows almost half of it is 'FF'.  What a waste! :-)


***********************************
*           Homevision            *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------
                          

Robot Fight                1           ER   4           X       J  ROBOFGHT
War 2000                   2               ??
Gogo? Home Monster         3               ??
World? Trap?               9               ??
Asteroid Fire              11              ??
Sky Alien                  12              ??
Base Attack                13               4           X          BASEATTK
Wall Break                 14              ??
Lilly Adventure            17               4           X       J  LILLY
Col 'N'                    ????             4           X       J  COLN
Cosmic War                 ????            ??
Frisco                     ????            ??
IQ 180                     ????             4           X       J  IQ180
Magic Carpet               ????            ?? 
Panda Chase                ????             4           X       J  PANDCHSE
Parachute                  ????             4           X          PARCHUTE
Plate Mania                ????            ??
Racing Car                 ????            ??
Repro Cart                 83014           ??
Tanks War                  ????            ??
Teddy Apple                ????            ??
Tennis Topsy               ????            ??
Zoo Fun                    ????             4           X       J  ZOOFUN


***********************************
*             Imagic              *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Trick Shot                 IA3000       R   4           X       J  TRICKSHT
Demon Attack               IA3200       C   4           X       J  DEMONATK
Star Voyager               IA3201       C   4           X          STARVYGR
Atlantis                   IA3203       U   4           X       J  ATLANTIS
Cosmic Ark                 IA3204       U   4           X          COSMCARK
No Escape!                 IA3312       R   4           X          NOESCAPE
Fire Fighter               IA3400       R   4           X       J  FIREFITE
Aerial Ace                 IA3409      ER  ??           
Shootin' Gallery           IA3410      ER   4           X       J  SHOOTIN
Riddle of the Sphinx       IA3600       U   4           X       J  RIDDLE
Dragon Fire                IA3611       C   4           X       J  DRGNFIRE
Fathom                     O3205        R   8      F8   X       J  FATHOM
Solar Storm                O3206        R   4           X          SOLRSTRM
Moonsweeper                O3207       ER   8      F8   X       J  MOONSWEP
Laser Gates                O3208        U   4           X       J  LASRGATE
Quick Step                 O3211        R   4           X       J  QUICKSTP
Subterra                   O3213        R   8      F8   X       J  SUBTERRA
Wing War                   EIZ-002-04  ER   8      F8   X       J  WINGWAR
Cubicolour                 ????        UR   4           X       J  CUBICOL
Imagic Selector            ????        DM   4           X      --  IMAGSLCT


***********************************
*               ITT               *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------
                                                             
Aliens Return              ????        ??   4           X       J  ALIENRET
Fire Birds                 ????        ??   4           X       J  FIREBIRD
Meteor Defence             ????        ??   4           X       J  METDEF

***********************************
*             Konami              *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Pooyan                     001         ER   4           X          POOYAN
Strategy X                 010         ER   4           X          STRATGYX
Marine Wars                011         ER   4           X          MARINWAR

Note:  The part numbers are labelled in binary notation. :-)

***********************************
*           M-Network             *
***********************************

[If SC is marked, chip uses an extra RAM, #TMM2009P-25. It's
really a 6116 in disguise]

         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Star Strike                MT4313       R   4           X       J  STARSTRK
Adventures of TRON         MT4317       U   4           X       J  ADVNTRON
MOTU: Power of He-Man      MT4319       R  16      E7   X       J  HE_MAN
Burgertime                 MT4518       R  12   X  E7   X       J  BURGTIME
Kool-Aid man               MT4648       U   4           X       J  KOOLAIDE
SC Football                MT5658       C   4           X       J  SUPRFOOT
Space Attack               MT5659       C   4           X       J  SPACATTK
Armour Ambush              MT5661       C   4           X       J  ARMAMBSH
TRON Deadly Discs          MT5662       U   4           X       J  TRONDEAD
Lock 'n Chase              MT5663       C   4           X       J  LOCKCHSE
Frogs and Flies            MT5664       U   4           X       J  FROGFLYS
SC Baseball                MT5665       C   4           X       J  SUPRBASE
Astroblast                 MT5666       U   4           X       J  ASTRBLST
Dark Cavern                MT5667       U   4           X       J  DARKCVRN
International Soccer       MT5687       U   4           X       J  INTRSCCR
Air Raiders                MT5861       R   4           X       J  AIRAIDRS
Bump 'n Jump               MT7045       R   8      E7   X       J  BNJ


'SC' in this case refers to extra RAM in the cart.

Notes:
 
All three E7 carts have been read in as 16K.  This makes it much easier
to write emulators and build hardware, as there's just one standard
size.  The RAM can still be included in any cart; however it has no
effect in Bump 'n Jump or He-Man.  Fortunately, it doesn't hinder operation
either, so I chose to just include the extra RAM under the E7 label.



***********************************
*          Milton Bradley         *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Survival Run               4362         R   4           X       J  SURVLRUN
Spitfire Attack            4363         U   4           X       J  SPITFIRE


***********************************
*        Mystique (hehe!)         *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Custer's Revenge           1001        ER   4           X       J  CUSTEREV
Bachelor Party             1002        ER   4           X       P  BACHELOR
Beat 'em and Eat 'em       1003        ER   4           X       P  BEATEM
Bachelorette Party         1004        ER   4           X       P  BACHLRTT
Gigolo                     1009        ER   4           X       J  GIGOLO
Jungle Fever               1011        ER   4           X       J  JNGLFEVR
Burning Desire             ????        ER   4           X       J  BURNDESR
Cathouse Blues             ????        ER   4           X       J  CATHOUSE
Knight on the Town         ????        ER   4           X       J  KNIGHTWN
Lady in Wading             ????        ER  ??                
Philly Flasher             ????        ER   4           X       P  PHILLY


***********************************
*            Mythicon             *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Sourcerer                  MA-1001      U   4           X       J  SORCERER
Fire Fly                   MA-1002      U   4           X       J  FIREFLY
Star Fox                   MA-1003      U   4           X       J  STARFOX

***********************************
*           Panda Inc.            *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Space Canyon               100          R   4           X       J  SPACANYN
Tank Brigade               101          R   4           X          TANKBRIG
Scuba Diver                104          R   4           X       J  SCUDIV_P
Stuntman                   105          R   4           X       J  STNTMAN
Dice Puzzle                106          R   4           X       J  DICEPUZL
Sea Hawk                   108          R   4           X          SEAHWK_P
Exocet                     109          R   4           X          EXOCET
Harbour Escape             110          R   4           X       J  HARBRESC


***********************************
*         Parker Bros.            *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

SW: Jedi Arena             PB5000       R   4           X       P  JEDIAREN
SW: Empire Strikes Back    PB5050       C   4           X       J  STAREMPR
SW: Death Star Battle      PB5060       R   8      E0   X       J  DETHSTAR
SW: Ewok Adventure         PB5065      PR  ??  ---Proto Exists---
Gyruss                     PB5080       R   8      E0   X       J  GYRUSS
James Bond 007             PB5110       R   8      E0   X       J  JAMEBOND
Frogger                    PB5300       C   4           X       J  FROGGER 
Amidar                     PB5310       U   4           X       J  AMIDAR
Super Cobra                PB5320       U   8      E0   X       J  SPRCOBRA
Reactor                    PB5330       U   4           X       J  REACTOR
Tutankham                  PB5340       U   8      E0   X       J  TUTANK
Sky Skipper                PB5350       R   4           X          SKYSKIPR
Q*Bert                     PB5360       C   4           X       J  QBERT_PB
Popeye                     PB5370       C   8      E0   X       J  POPEYE
SW: Arcade Game            PB5540      ER   8      E0   X       J  SWARCADE
Q*Bert's Qubes             PB5550      ER   8      E0   X       J  QBRTQUBE
Frogger ][: Threeedeep     PB5590      ER   8      E0   X       J  FROGGER2
Circus Charlie             PB5750          ??  ---No Known Copies Exist---
Montezuma's Revenge        PB5760      ER   8      E0   X       J  MONTZREV
Mr. Do's Castle            PB5820      ER   8      E0   X       J  DOCASTLE
Spider-Man                 PB5900       R   4           X       J  SPIDRMAN
Strawberry Shortcake       PB5910       U   4           X       J  STRWBERY
GI Joe: Cobra Strike       PB5920       U   4           X       J  GIJOE
Action Force               PB5920      ER   4           X       J  ACTIONMN
Lord of the Rings          PB????      UR  ??  ---No Known Copies Exist---
McDonald's                 PB????      UR  ??  ---No Known Copies Exist---


***********************************
*          Playaround             *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

See Mystique for #201-205

/General retreat           206         ER  ??
\Westward Ho               206         ER  ??


***********************************
*             Puzzy               *
***********************************

         
         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Seesaw                     ????        ER   4           X       J  SEESW_TW
Football                   ????        ER   4           X          FB_PIR8
Earth Attack               ????        ER  ??
Puzzled World              ????        ER   4           X          PUZZL_TW
Chess                      ????        ER  ??
Boom Bang                  ????        ER   4           X       J  BOOMBANG
Pitfall                    ????        ER  ??
Spider                     ????        ER  ??
Tennis                     ????        ER  ??
Pyramid War                ????        ER   4           X          PYRMDWAR
Bobby is Going Home        ????        ER   4           X       J  BOBBY
Mr. Postman                ????        ER   4           X          MRPOSTMN
Space Tunnel               ????        ER   4           X          SPACTUNL
Fancy Car                  ????        ER  ??
My Way                     ????        ER  ??
S.O.S.                     ????        ER  ??
Frogger                    ????        ER  ??
Fishing                    ????        ER  ??
Cross Force                ????        ER   4           X          CROSFRCE
Farmer Dan                 ????        ER  ??
Dancing Plate              ????        ER   4           X          DANCPLAT
Volley Ball                ????        ER  ??
Little Bear                ????        ER  ??


***********************************
*         Rainbow Vision          *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Pac-Kong                   55-003       R   4           X          PACKONG
Netmaker                   55-006       R  ??
Mafia                      55-010       R  ??
Hey! Stop!                 55-012       R   4           X       J  HEY_STOP
Bi! Bi!                    55-013       R   4           X          BIBI
Catch Time                 55-015       R  ??
Boom Bang                  55-016       R   4           X          BOOMBANG
Mariana                    55-017       R  ??
Curtiss                    55-019       R  ??
Tuby Bird                  55-020       R   4           X          TUBYBIRD
Tomboy                     55-???       R   4           X       J  TOMBOY


***********************************
*               Salu              *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Acid Drop                  ????        ER  16      F6   X       J  ACIDDROP
Ghostbusters ][            ????        ER  16      F6   X       J  GHOSTBS2
Pick 'N' Pile              ????        ER  16      F6   X       J  PICKPILE


***********************************
*              Sancho             *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Exocet                     TEC001      ER  ??
Sea Hawk                   TEC002      ER  ??
Skin Diver                 TEC003      ER   4           X          SKINDIVR
Nightmare                  TEC004      ER   4           X          NGHTMARE
Dice Puzzle                TEC005      ER   4           X          DICEPUZL
Forest                     TEC006      ER   4           X          FOREST


***********************************
*              Sega               *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------
                            
Tac-Scan                   001-01       U   4           X          TACSCAN
Sub Scan                   002-01       U   4           X          SUBSCAN
Thunderground              003-01       R   4           X          THUNDGRD
Star Trek: SOS             004-01       R   8      F8   X       J  STARTREK
Buck Rodgers               005-01       U   8      F8   X       J  BUCKROG
Congo Bongo                006-01       R   8      F8   X       J  CONGBONG
Up 'n Down                 009-01      ER   8      F8   X       J  UPNDOWN
Tapper                     010-01      ER   8      F8   X       J  TAPPER
Spy Hunter                 011-02      ER   8      F8   X       J  SPYHUNTR


***********************************
*        Selchow & Righter        *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Glib                       ????        UR   4           X          GLIB


***********************************
*             Simage              *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Eli's Ladder               ????        UR   4           X          ELILADDR


***********************************
*            Sparrow              *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Music Machine              GCG 1001T   UR   4           X          MUSCMACH


***********************************
*         Spectravision           *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Gangster Alley             SA-201       R   4           X       J  GANGALLY
Planet Patrol              SA-202       R   4           X       J  PLANTPAT
Cross Force                SA-203       R   4           X          CROSFRCE
Tape Worm                  SA-204       R   4           X       J  TAPEWORM
China Syndrome             SA-205       R   4           X          CHINASYN
The Challange of... Nexar  SA-206       R   4           X          NEXAR
Master Builder             SA-210       R   4           X       J  MASTBULD
Galactic Tactic            SA-211       R  ??
Mangia                     SA-212       R   4           X       J  MANGIA
Gas Hog                    SA-217      ER   4           X       J  GASHOG
Bumper Bash                SA-218      ER   4           X       J  BUMPER
Save the Whales            SA-???      UR  ??
Cave-In                    SA-???      UR  ??
Chase the Chuckwagon       SA-???      UR   4           X       J  CHUCKWGN


***********************************
*           StarPath              *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Supercharger BIOS ROM      330030      --   2           X          STARPATH
Phasor Patrol              RA-4000      R   6      AR   X       J  PHASOR
Communist Mutants          RA-4001      R   6      AR   X          COMMIE
Suicide Mission            RA-4002      R   6      AR   X       J  SUICIDE
Killer Satellites          RA-4103      R   6      AR   X       J  KILLRSAT
Rabbit Transit             RA-4104     ER   6      AR   X       J  RABBIT
Frogger                    RA-4105     ER   6      AR   X       J  FROGGER
Escape from the Mindmaster RA-4200     ER   6*4    AR   XXXX    J  MINDMAS1-4
Sword of Saros             RA-4201     ER   6      AR   X          SWOSAROS
Excalibur                  RA-4201     PR  ??
Fireball                   RA-4300      R   6      AR   X          FIREBALL
Party Mix                  RA-4302     ER   6*3    AR   XXX        PRTYMIX1-3
Dragonstomper              RA-4400     ER   6*3    AR   XXX     J  DRAGON1-3
Survival Island            RA-4401     ER   6*3    AR   XXX        SURVIVAL1-3
Sweat!                     RA-4???     PR   6*2    AR   XX         SWEAT1,2
Comm. Mutants Demo         ????        DM   6      AR   X          COMMDEMO
Dragonstomper Demo         ????        DM   6      AR   X          DRAGDEMO
Fireball Demo              ????        DM   6      AR   X          FIREDEMO
Frogger Demo               ????        DM   6      AR   X          FROGDEMO
Killer Satellites Demo     ????        DM   6      AR   X          KILLDEMO
Esc. from Mindmaster Demo  ????        DM   6      AR   X          MINDDEMO
Party Mix Demo             ????        DM   6      AR   X          PRTYDEMO
Rabbit Transit Demo        ????        DM   6      AR   X          RABTDEMO
Suicide Mission Demo       ????        DM   6      AR   X          SUICDEMO


***********************************
*            Sunrise              *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Quest for Quinta Roo       1603        ER   8      F8   X       J  QUINTROO
Snowplow                   ????        ER  ??
Glacier Patrol             ????        ER   4           X       J  GLACIER
Noah and the Ark           ????            ??


***********************************
*             Suntek              *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Time Race                  1           ER  ??
Galactic                   2           ER  ??
Pac-Kong                   3           ER   4           X          PACKONG
Pyramid War                4           ER   4           X          PYRMDWAR
Netmaker                   6           ER  ??
Bermuda                    9           ER   4           X          BERMUDA


***********************************
*         Technovision            *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Adventures of GX-12        ????            ??
Flipper                    ????            ??
Formula I                  ????            ??
Jungle Jim                 ????            ??
Laser Raid                 ????            ??
Moonbase                   ????            ??
Motor Mouth                ????            ??
Mouse Highway              ????             4           X       J  CATMOUSE
Nuts                       ????             4           X          NUTS
Pharoah's Curse            ????             4           X          PHARHCRS
Save Our Ship              ????             4           X       J  SAVESHIP
Silly Safari               ????            ??
Shoot-Out                  ????            ??
Stone Age                  ????            ??
Tachion Beam               ????            ??


***********************************
*           Tele-Games            *
***********************************
         
         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------


Bogey Blaster               5861 A030   R   4           X       J  BOGYBLST
Night Stalker               ????        R   4           X       J  NIGHTSTK
Universal Chaos             ????            4           X       J  UNIVCHOS
Bump 'n Jump                ????            8      F8   X  X1   J  BUMPHUMP


X1: Intresting... uses F8 instead of E7 bankswitching!


***********************************
*            Telesys              *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Coco Nuts                  1001         R   4           X       J  COCONUTS
Cosmic Creeps              1002         R   4           X       J  COSMCREP
Fast Food                  1003         R   4           X       J  FASTFOOD
Ram- It                    1004        ER   4           X       J  RAMIT
Star Gunner                1005        ER   4           X          STARGN
Demolition Herby           1006        ER   4           X       J  DEMOHRBY
Bouncing Baby Monkeys      ????        UR  ??
The Impossible Game        ????        UR  ??


***********************************
*          Tigervision            *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

King Kong                  7-001        R   4           X       J  KINGKONG
Jawbreaker                 7-002        R   4           X       J  JAWBREAK
Threshold                  7-003        R   4           X       J  THRSHOLD
River Patrol               7-004       UR   8      3F   X       J  RIVERP
Marauder                   7-005       ER   4           X       J  MARAUDER
Springer                   7-006       UR   8      3F   X       J  SPRINGER
Polaris                    7-007       ER   8      3F   X       J  POLARIS
Miner 2049'er              7-008       ER   8      3F   X       J  MNR2049R
Intuition                  7-009       NR  ??
Scraper Caper              7-010       NR  ??
Miner 2049'er Volume ][    7-011       ER   8      3F   X       J  MINRVOL2
Espial                     7-012       ER   8      3F   X       J  ESPIAL

***********************************
*            TNT Games            *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------
                          
BMX Airmaster              26192       ER  16      F6   X       J  BMX_TNT

***********************************
*        20th Century Fox         *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------
                          
Worm War I                 11001        U   4           X       J  WORMWAR1
Beany Bopper               11002        R   4           X       J  BEANYBOP
Fast Eddie                 11003        U   4           X          FASTEDIE
Deadly Duck                11004        R   4           X       J  DEADDUCK
Mega Force                 11005        R   4           X          MEGAFRCE
Alien                      11006        R   4           X       J  ALIEN
Turmoil                    11007        U   4           X       J  TURMOIL
Fantastic Voyage           11008        R   4           X          FANTCVOY
Crypts of Chaos            11009        R   4           X          CRPTCHOS
M*A*S*H                    11011        U   4           X       J  MASH
Bank Heist                 11012        R   4           X          BANKHEST
Porky's                    11013        R   8      F8   X       J  PORKYS
Flash Gordon               11015        R   4           X          FLASHGRD
Revenge of the BS Tomatoes 11016        R   4           X       J  REVNGTOM
The Earth Dies Screaming   11020        R   4           X       J  EARTHDIE
Spacemaster X-7            11022       ER   4           X          SPACMAST
Meltdown                   11029       ER  ??           Need!
Crash Dive                 11031       ER   4           X          CRSHDIVE
Alligator People           ?????       PR   4           X       J  ALIGPEPL

***********************************
*           U.S. Games            *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Space Jockey               VC 1001      C   2           X       J  SPACJOCK
Sneak 'n Peek              VC 1002      R   4           X          SNEKPEEK
Word Zapper                VC 1003      U   4           X       J  WORDZAPR
Commando Raid              VC 1004      R   4           X          COMANDRD
"Name This Game"           VC 1007      R   4           X  X1   J  NAMEGAME
Octopus                    VC 1007     ER   4           X  X1   J  NAMEGAME
Towering Inferno           VC 1009      U   4           X       J  TOWERINF
M.A.D                      VC 1012      R   4           X       J  M_A_D
Gopher                     VC 2001      R   4           X       J  GOPHER
Squeeze Box                VC 2002     ER   4           X       J  SQUEEZBX
Eggomania                  VC 2003      R   4           X       P  EGGOMANA
Picnic                     VC 2004     ER   4           X       P  PICNIC
Piece 'o Cake              VC 2005     ER   4           X       P  PIECECKE
Raft Rider                 VC 2006     ER   4           X       J  RAFTRIDR
Entombed                   VC 2007     ER   4           X       J  ENTOMBED
                           

X1: These two carts are the same except the name.


***********************************
*        Universal Gamex          *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

X-Man                      GX-001      UR   4           X       J  XMAN


***********************************
*         Venturevision           *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Rescue Terra I             VV2001      ER   4           X          RESCTER1
Inner Space                ????


***********************************
*           Video Gems            *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Missile Control            ????        ER   4           X          MISLCONT
Mission Survive            ????        ER
Steeple Chase              ????        ER
Surfer's Paradise          ????        ER   4           X          SURFPRDS
Treasure Below             ????        ER


***********************************
*         Wizard Video            *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Texas Chainsaw Massacre    008         ER   4           X       J  TXSCHAIN
Halloween                  007         ER   4           X       J  HALOWEEN


***********************************
*             Xonox               *
***********************************

Note: No repeats are listed (i.e. when the same game is on two double-enders)

         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Spike's Peak               99001       ER   8      F8   X       J  SPIKE_PK
Ghost Manor                99002       ER   8      F8   X       J  GHOSTMAN
Chuck Norris Super Kicks   99003       ER   8      F8   X       J  CHUCKICK
Artillery Duel             99004       ER   8      F8   X       J  ART_DUEL
Robin Hood                 99005       ER   8      F8   X       J  ROBH_P
Sir Lancelot               99006       ER   8      F8   X       J  SIRL_N
Tomarc the Barbarrian      99007        R  ??
Motocross Racer            99008        R  ??


***********************************
*             Zellers             *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Time Warp                  ????        ER   4           X          TIMEWARP

***********************************
*             Zimag               *
***********************************


See Emag


***********************************
*       Un-marked / Other         *
***********************************


         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Dragon Defender            TP-605      ER   4           X          DRGNDFND
Hole Hunter                TP-606      ER   4           X          HOLEHUNT
Farmyard Fun               TP-617      ER   4           X          FARMYARD
16 Games in 1              ????        ER  ??
Brick Kick                 ????        ER   4           X       J  BRICKICK
Challange                  ????        ER   4           X          CHALENGE
Clown Down Town            ????        ER   4           X          CLWNDOWN
Criminal Pursuit           ????        ER   4           X          CRIMLPUR
Dragon Treasure            ????        ER  ??
Frontline                  ????        ER  ??
Inca Gold                  ????        ER  ??
laser Volley               ????        ER   4           X          LASRVOLY
Ski Run                    ????        ER   4           X          SKI_RUN
Lie Low                    ????        ER  ??
Lost and Found             ????        ER  ??
Missile Attack             ????        ER  ??
Oops                       ????        ER  ??
Planet Protector           ????        ER  ??
Ski Hunt                   ????        ER   4           X          SKIHUNT
Super-Ferrari              ????        ER   4           X          SUPFERRI
Tom Boy                    ????        ER   4           X       J  TOMBOY
UFO Patrol                 ????        ER   4           X          UFOPATRL
Wolf Fighting              ????        ER  ??
Pink Panther / Probe 2000  ????        PR  ??   ---One Proto Exists---


**********************************************
*  Odd proto's and other intresting things   *
**********************************************

              Name / Description           SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Air Raid (Not Air Raiders! By Men-A-Vision) 4           X          AIR_RAID
Bira Bira  (modified Burning Desire)        4           X       J  BIRABIRA
Challange of Nexar (Changed GFX)            4           X          NEXAR-CH
Circus Atari  (Uses joysticks)              4           X       J  CIRCUS-J
Colourbar Generator                         4           X      --  COLORBAR
Condor  (Similar to Condor Attack)          4           X          CONDOR
Donkey Kong  (Changed GFX)                  4           X       J  DK-CH
Duck Shot  (Positively Weird!)              4           X          DUCKSHOT
Elk Attack  (Mark R. Hahn; 1987)            8      F8   X          ELK
Fishing Derby (changed GFX; from 32-in-1)   2           X       J  FISHN-CH
Freeway  (Changed GFX; from the 32-in-1)    2           X       J  FREWY-CH
Galaga  (River Raid ripoff)                 4           X       J  GALAGA
Joust Hack  (#1)                            8      F8   X       J  J_HCK1
Joust Hack  (#2)                            8      F8   X       J  J_HCK2
Joust; Super  (modified)                    8      F8   X       J  SJOUST
Marflegr  (PAL version of Sea Hawk)         4           X          MARFLEGR
MASH w/ subs  (Modified GFX)                4           X       J  MASH_SUB
Missile Command  (Changed GFX)              4           X       J  MC-CH
Pac-Kong  (Changed GFX)                     4           X       J  PK-CH
Snail Against Squirrel  (Changed GFX)       4           X       J  SVS-CH
Space Raid (Vaguely Threshold-like)         4           X          SPACRAID
Test  (Neat little TIA test)                4           X      --  TEST
Traffic  (Dunno; intresting)                4           X       J  TRAFFIC
World End (I Believe this is World? Trap?)  4           X       J  WORLDEND
Galactic  (Starsoft title; not a pirate)    4           X       J  GALACTIC                                        
Air-sea Battle (changed GFX)                2           X       J  AIRSEA2
Space Invaders (changed GFX)                4           X       J  SPACE2
Magazine Demo                               4           X      --  MAGDEMO
4-game ROM... uses F6 bankswitch           16      F6   X       J  GORF_RIP

***********************************
*            Overflow             *
***********************************

Used to keep Bankzilla's database program happy (mainly PAL versions and
game revisions.  Prevents cluttering up the main listing)

              
         Name                Part #    RA  SZ  SC  BS  IM  SP  CT  Filename
---------------------------------------------------------------------------

Pal version of Pele's Soc. CX2616      ??   4           X       J  CX2616PL
Taiwanese Enduro           ????        ??   4           X       J  ENDRO_TW    
Taiwanese Pele's           ????        ??   4           X       J  PELE_TW
CCE version of Pitfall     ????        ??   4           X       J  PITF_CCE
Taiwanese River Raid       ????        ??   4           X       J  RIVER_TW
Diagnostic Program         ????        ??   4           X      --  SALTDIAG
Superman; revision 2       ????        ??   4           X       J  SUPRMAN2
Dunno                      ????        ??   2           X      --  XX
Pal version of KLAX        ????        ??  16   X  F6   X       J  KLAX
Okie Dokie; Limited        ????        ??   4           X       J  OKIEDLIM
Robot Tank; F8-fixed       ----        --   8      F8   X       J  ROBO_FIX
Surf's Up; original        ----        --   8      F8   X       J  SURFSUP
Dumbo's F. Circ.; PAL      ????        PR   8      F8   X       J  DUMBO_P
Sir Lancelot; NTSC         ????        ??   8      F8   X       J  SIRL_P
Smurf Rescue; PAL          ????        ??   8      F8   X       J  SMURF_PL
'Chess'; Taiwanese         ????        ??   2           X       J  CHESS_TW
Outerspace; Sears          ????        ??   2           X       J  OUTERSPC
Okie Dokie Proto           ----        --   2           X       J  LT
Taiwanese Bowling          ????        ??   2           X       J  BOWLG_TW

*Not* a ROM image!         ****        **  **  **  **   X      --  EMPTY

####Data End####

***********************************
*    World of Dead ROM Images     *
***********************************

Yeppers, some ROM images are better off dead.  Either they're exact copies,
damaged, or just plain DOA.

XTACK      Same as Z-Tack by Bomb.
POLEPOS    Pole Position... bad read.  5 bytes bad.
ASTROSMS   Same as M-net's Astroblast

***********************************
*          Proto's Read           *
***********************************


Thanks to many people, I have been able to read some prototype carts in!
However, the data contained on the carts is identical to the production
ones.

Bermuda Triangle, #371
Pac-Man, in an Imagic case!!  The board has been wave-soldered; said board
         is an actual Imagic board, and the chip is a ROM.  