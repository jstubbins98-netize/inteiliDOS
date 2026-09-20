/*
 * inteiliDOS -- still_alive_easter_egg/stillalive.c
 * "Still Alive" Easter egg — Portal end-credits faithful recreation
 *
 * Visual + audio run together:
 *   1. Screen clears to amber-on-black; frame and right column draw instantly.
 *   2. Each lyric character is typed onto the left column at the moment its
 *      corresponding note plays — characters are spread evenly across all notes
 *      so the last character appears with the last note.
 *   3. The right-panel ASCII art image changes at key moments in the melody,
 *      matching the transitions from the original QBasic STILL ALI.BAS program
 *      (Hudson Green, hudsongreen.com): Aperture Science logo → radiation →
 *      broken heart → explosion → fire → checkmark → atom → GLaDOS face →
 *      cake, and back, following the song's emotional arc.
 *
 * "This was a triumph."  --  GLaDOS, Portal (2007)
 */

#include "stillalive.h"
#include "../kernel/timer.h"
#include "../kernel/vga.h"
#include <stdint.h>

/* =========================================================================
 * Screen layout (80 x 25)
 *
 *   col  0     : '|'   left border
 *   cols 1-38  : left content  (38 chars)
 *   cols 39-40 : '||'  centre divider
 *   cols 41-78 : right content (38 chars)
 *   col  79    : '|'   right border
 *
 *   row  0     : top border
 *   rows 1-23  : content
 *   row  24    : bottom border
 *
 * Right panel sub-layout:
 *   rows  1-9  : developer name credits  (9 rows)
 *   row  10    : separator  ---
 *   rows 11-23 : animated ASCII art      (13 rows = ART_ROWS)
 * ========================================================================= */
#define LEFT_START   1
#define LEFT_END    38
#define DIV_COL     39
#define RIGHT_START 41
#define RIGHT_END   78
#define BORDER_R    79
#define TOP_ROW      0
#define BOT_ROW     24

#define ART_ROWS          13
#define ART_PANEL_START   11   /* first VGA row used for art */
#define NAMES_ROWS         9   /* rows 1-9 */

/* =========================================================================
 * Helpers
 * ========================================================================= */
static void sa_color(void) { vga_set_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK); }

static void sa_fill(int row, int col, int end_col, char ch) {
    vga_set_cursor(row, col);
    for (int c = col; c <= end_col; c++) vga_putchar(ch);
}

static void sa_str(int row, int col, int end_col, const char *s) {
    vga_set_cursor(row, col);
    for (int c = col; *s && c <= end_col; s++, c++) vga_putchar(*s);
}

/* =========================================================================
 * Frame (borders)
 * ========================================================================= */
static void sa_draw_frame(void) {
    vga_set_cursor(TOP_ROW, 0);        vga_putchar('+');
    sa_fill(TOP_ROW, 1, LEFT_END,     '-');
    vga_set_cursor(TOP_ROW, DIV_COL); vga_puts("++");
    sa_fill(TOP_ROW, RIGHT_START, RIGHT_END, '-');
    vga_set_cursor(TOP_ROW, BORDER_R); vga_putchar('+');

    vga_set_cursor(BOT_ROW, 0);        vga_putchar('+');
    sa_fill(BOT_ROW, 1, LEFT_END,     '-');
    vga_set_cursor(BOT_ROW, DIV_COL); vga_puts("++");
    sa_fill(BOT_ROW, RIGHT_START, RIGHT_END, '-');
    vga_set_cursor(BOT_ROW, BORDER_R); vga_putchar('+');

    for (int r = 1; r <= 23; r++) {
        vga_set_cursor(r, 0);        vga_putchar('|');
        vga_set_cursor(r, DIV_COL);  vga_puts("||");
        vga_set_cursor(r, BORDER_R); vga_putchar('|');
    }
}

/* =========================================================================
 * Right panel — developer names (rows 1-9) + separator (row 10)
 * ========================================================================= */
static const char *const sa_names[] = {
    " Ieztyn Bleasdale-Shepherd",
    " Chris Bokitch",
    " Steve Bond",
    " Matt Boone",
    " Antoine Bourdon",
    " Jamaal Bradley",
    " Jason Brashill",
    " Charlie Brown",
    " Charlie Burgin",
};
#define SA_NAMES ((int)(sizeof(sa_names)/sizeof(sa_names[0])))

static void sa_draw_names(void) {
    for (int i = 0; i < SA_NAMES && i < NAMES_ROWS; i++)
        sa_str(i + 1, RIGHT_START, RIGHT_END, sa_names[i]);
    sa_fill(10, RIGHT_START, RIGHT_END, '-');
}

/* =========================================================================
 * Animated ASCII art — 10 images sourced from STILLALI.BAS (Hudson Green)
 *
 * Each array: ART_ROWS (13) strings, each <= 38 chars wide, displayed on
 * VGA rows ART_PANEL_START (11) through 23.
 *
 * Art IDs:
 *   0 APLABS    Aperture Science circular logo
 *   1 ATOM      Atom / orbital rings symbol
 *   2 RADIATION Radiation / nuclear trefoil
 *   3 BHEART    Broken heart
 *   4 FIRE      Flames
 *   5 CHECK     Checkmark / tick
 *   6 EXPLODE   Explosion
 *   7 BLKMESA   Black Mesa facility silhouette
 *   8 CAKE      The cake (which is a lie)
 *   9 GLADOS    GLaDOS AI face
 * ========================================================================= */

static const char *const art_aplabs[ART_ROWS] = {
    "        . :H@@@MM@M#H/.,+%;,          ",
    "    -+@MM; $M@@MH+-,;XMMMM@MMMM@+-    ",
    "   ;@M@@M- XM@X;. -+XXXXXHHH@M@M#@/.  ",
    " -@#@@@MX .,              -%HX$$%%%+; ",
    "=-./@M@M$                  .;@MMMM@MM:",
    "@M@H: :@:                    . -X#@@@@",
    "@@@MMX, .                    /H- ;@M@M",
    "/MMMM@MMH/.                  XM@MH; -;",
    " /%+%$XHH@$=              , .H@@@@MX, ",
    "  .%MM@@@HHHXX$$$%+- .:$MMX -M@@MM%.  ",
    "    =XMMM@MM@MM#H;,-+HMM@M+ /MMMX=    ",
    "        ,:+$+-,/H#MMMMMMM@- -,        ",
    "              =++%%%%+/:-.            ",
};

static const char *const art_atom[ART_ROWS] = {
    "               +:    //               ",
    "             -X        H.             ",
    "//;;;:;;-,   X=        :+   .-;:=;:;%;",
    "%           :%.=/++++/=.$=           %",
    ",%;         %/:+/;,,/++:+/         ;+.",
    "     ;+;;/= @.  .H##X   -X :///+;     ",
    "     ;+=;;;.@,  .XM@$.  =X.//;=%/.    ",
    ",%=         %;-///==///-//         =%,",
    "+           :%-;;;;;;;;-X-           +",
    ":;;::;;-.    %-        :+    ,-;;-;:==",
    "             ,X        H.             ",
    "               //    +;               ",
    "                ,////,                ",
};

static const char *const art_radiation[ART_ROWS] = {
    "         ,@################+          ",
    "            X############/            ",
    "             $##########/             ",
    "               /X/;;+X/               ",
    "                                      ",
    "                -XHHX-                ",
    "               ,######,               ",
    "#############-   -//-   -#############",
    "##############%,      ,+##############",
    "%############%          %############%",
    " %##########;            ;##########% ",
    "   .+M###@,                ,@###M+.   ",
    "      :XH.                  .HX:      ",
};

static const char *const art_bheart[ART_ROWS] = {
    "                       ,/XM#MMMX;,    ",
    "                    -@######%  $###@= ",
    "     .,--,         -H#######$   $###M:",
    "/@###########H=      ;################",
    "+#############M/,      %##############",
    "################      .M#############;",
    "###############M      ,@###########M:.",
    "@##################%-     +######$-   ",
    ";##################X     .X#####+,    ",
    "  ,;X##############,       .MM/       ",
    "     ,:+$H@M#######M#$-    .$$=       ",
    "                 .,/X$;   .::,        ",
    "                     .,    ..         ",
};

static const char *const art_fire[ART_ROWS] = {
    "                   .H##H,             ",
    "               .+#########H.          ",
    "             -$############@.         ",
    "         .$##################:  @#@-  ",
    "    ,;  .M###################;  H###; ",
    "-M###.  M#################@.  ;######H",
    "M####-  +###############$   =@#######X",
    " /####X-   =########%   :M########@/. ",
    "   ,;%H@X;   .$###X   :##MM@%+;:-     ",
    " -/;:-,.              ,,-==+M########H",
    "-##################@HX%%+%%$%%%+:,,   ",
    "XHX%:#####MH%=    ,---:;;;;/&&XHM,:###",
    "@#MX %+;-                             ",
};

static const char *const art_check[ART_ROWS] = {
    "                                 :X###",
    "                             ;M######X",
    "                           -@########$",
    "                        =M############",
    "                       +##############",
    "        ,/:         ,M##########M;.   ",
    "     -+@###;       =##########M;      ",
    "$M###########;   :########/           ",
    ",;X###########; =#######$.            ",
    "      ,+#############+                ",
    "         /M########@-                 ",
    "             +####:                   ",
    "              ,$M-                    ",
};

static const char *const art_explode[ART_ROWS] = {
    "            /M;\\                      ",
    "             -###H-          -@/      ",
    "              %####$.  -;  .%#X\\      ",
    ".          .+/;%#############-        ",
    "-/%H%+;-,    +##############/\\        ",
    "       -/H#####################H+=.   ",
    "          .+#################X.\\      ",
    "           /@###############+;;/%%;,  ",
    "        -%###################$\\       ",
    "   ,%#####MH$%;+#####M###-/@####%     ",
    " :$H%+;=-      -####X.,H#   -+M##@-\\ ",
    "               .#H,               :XH,",
    "                +                   .;",
};

static const char *const art_blkmesa[ART_ROWS] = {
    "       ,;X@@X%/;=----=:/%X@@X/,       ",
    "   -XMX:                      =XMX=   ",
    "  /@@:                          =H@+  ",
    "+@X.                               $@%",
    "@@,                                .@@",
    "@:                                  :@",
    "@:         :HHHHHHHHHHHHHHHHHHX,    =@",
    "@@,        :@@@@@@@@@@@@@@@@@@@@@= .@@",
    "+@X        :@@@@@@@@@@@@@@@M@@@@@@:%@%",
    "  +@@HHHHHHH@@@@@@@@@@@@@@@@@@@@@@@+  ",
    "   =X@@@@@@@@@@@@@@@@@@@@@@@@@@@@X=   ",
    "       ,;$@@@@@@@@@@@@@@@@@@X/-       ",
    "          .-;+$XXHHHHHX$+;-.          ",
};

static const char *const art_cake[ART_ROWS] = {
    "           /M/              .,-=;//;- ",
    "     -$##@+$###@H@MMM#######H:.    -/H",
    ".,H@H@ X######@ -H#####@+-     -+H###@",
    "%-  :M##########$.    .:%M###@%:      ",
    "##H,   +H@@@$/-.  ,;$M###@%,          ",
    "##################@/.         :%H##@$-",
    "###############H,         ;HM##M$=    ",
    "###############H..;XM##M$=          .:",
    "###################@%=           =+@MH",
    "+M###############M,      ,/X#H+:,     ",
    " .;XM###########H=   ,/X#H+:;         ",
    "        ,:/%XM####H/.                 ",
    "             ,.:=-.                   ",
};

static const char *const art_glados[ART_ROWS] = {
    ".    .X  X.%##@;# #   +@#######X. @H% ",
    " :H##M%:=##+ .M##M,;#####/+#######% ,M",
    ".M########=  =@#@.=#####M=M#######=  X",
    "            @##..###:.    .H####. @@ X",
    "  ############: ###,/####;  /##= @#. M",
    "%=   ######M## ##.M#:   ./#M ,M #M ,#$",
    "#/         $## #+;#: #### ;#/ M M- @# ",
    "     ######/.: #%=# M#:MM./#.-#  @#: H",
    ",.=   @###: /@ %#,@  ##@X #,-#@.##% .@",
    "  ;###M#@ M###H .#M-     ,##M  ;@@; ##",
    "  .M#M##H ;####X ,@#######M/ -M###$  -",
    "     H#M    /@####/      ,++.  / ==-, ",
    "              ,=/:, .+X@MMH@#H  #####$",
};

/* =========================================================================
 * Art schedule: switch the right-panel image at these note indices
 *
 * Note indices were computed from the MIDI note table timing to match
 * the SUB call sites in STILLALI.BAS:
 *   APLABS    → "Aperture Science" / "We do what we must"
 *   RADIATION → "Except the ones who are dead"
 *   BHEART    → "broke my heart"
 *   EXPLODE   → "And tore me to pieces" / "So I'm GLaD I got burned"
 *   FIRE      → "threw every piece into a fire"
 *   CHECK     → "I was so happy for you!"
 *   ATOM      → "And the Science gets done"
 *   GLADOS    → transition / "Look at me still talking"
 *   CAKE      → "this cake is great" / outro
 * ========================================================================= */
typedef struct { int note_idx; int art_id; } ArtChange;

static const ArtChange art_schedule[] = {
    {   0, 0 },  /* APLABS    — opening                                  */
    { 111, 0 },  /* APLABS    — "Aperture Science"          @25.8s       */
    { 153, 2 },  /* RADIATION — "Except the ones who are dead" @34.8s   */
    { 169, 0 },  /* APLABS    — "But there's no sense crying"  @37.6s   */
    { 254, 3 },  /* BHEART    — "broke my heart"               @59.3s   */
    { 270, 6 },  /* EXPLODE   — "And tore me to pieces"        @62.8s   */
    { 281, 4 },  /* FIRE      — "into a fire"                  @67.1s   */
    { 301, 5 },  /* CHECK     — "I was so happy for you!"      @72.0s   */
    { 317, 6 },  /* EXPLODE   — "So I'm GLaD I got burned"     @77.1s   */
    { 329, 1 },  /* ATOM      — "And the Science gets done"    @80.3s   */
    { 339, 0 },  /* APLABS    — "for the people who are alive" @83.8s   */
    { 350, 9 },  /* GLADOS    — outro transition               @87.0s   */
    { 364, 8 },  /* CAKE      — "the cake" ending              @90.8s   */
};
#define ART_SCHED_COUNT ((int)(sizeof(art_schedule)/sizeof(art_schedule[0])))

/* Lookup: art_id → pointer array */
static const char *const *const ART_PTRS[10] = {
    art_aplabs, art_atom,   art_radiation, art_bheart, art_fire,
    art_check,  art_explode, art_blkmesa,  art_cake,   art_glados,
};

/* Draw one art image into rows ART_PANEL_START … ART_PANEL_START+ART_ROWS-1 */
static void sa_draw_art(int art_id) {
    if (art_id < 0 || art_id >= 10) return;
    const char *const *rows = ART_PTRS[art_id];
    for (int i = 0; i < ART_ROWS; i++) {
        /* Clear row first, then write art (handles varying line lengths) */
        sa_fill(ART_PANEL_START + i, RIGHT_START, RIGHT_END, ' ');
        sa_str (ART_PANEL_START + i, RIGHT_START, RIGHT_END, rows[i]);
    }
}

/* =========================================================================
 * Lyric lines for the left column
 * NULL text = blank row (border character only, no chars to type)
 * ========================================================================= */
typedef struct { int row; const char *text; } LyricLine;

static const LyricLine sa_lyrics[] = {
    { 1,  " Form FORM-29827281-12."        },
    { 2,  " Test Assessment Report"         },
    { 3,  NULL                             },
    { 4,  NULL                             },
    { 5,  " This was a triumph."           },
    { 6,  " I'm making a note here:"       },
    { 7,  " HUGE SUCCESS."                 },
    { 8,  " It's hard to overstate"        },
    { 9,  " my satisfaction."              },
    {10,  " Aperture Science"              },
    {11,  " We do what we must"            },
    {12,  " because we can."               },
    {13,  " For the good of all of us"     },
    {14,  " Except the ones who are dead." },
    {15,  " But there's no sense crying"   },
    {16,  " over every mistake."           },
    {17,  " You just keep on trying"       },
    {18,  " till you run out of cake."     },
    {19,  " And the Science gets done."    },
    {20,  " And you make a neat gun."      },
    {21,  " For the people who are"        },
    {22,  " still alive."                  },
    {23,  NULL                             },
};
#define SA_LYRIC_COUNT ((int)(sizeof(sa_lyrics)/sizeof(sa_lyrics[0])))

/* ── flat (row, col, char) buffer built at runtime ── */
typedef struct { int row; int col; char ch; } LyricChar;
static LyricChar lc_buf[512];
static int       lc_total = 0;

static void build_lyric_chars(void) {
    lc_total = 0;
    for (int i = 0; i < SA_LYRIC_COUNT; i++) {
        if (!sa_lyrics[i].text) continue;
        int col = LEFT_START;
        for (const char *s = sa_lyrics[i].text; *s && col <= LEFT_END; s++, col++) {
            if (lc_total >= 512) break;
            lc_buf[lc_total].row = sa_lyrics[i].row;
            lc_buf[lc_total].col = col;
            lc_buf[lc_total].ch  = *s;
            lc_total++;
        }
    }
}

/* =========================================================================
 * Note table (from Still_alive.mid, highest-note-wins per time slice)
 * freq == 0 → silence (rest)
 * ========================================================================= */
typedef struct { unsigned int freq; int ms; } Note;

static const Note still_alive_melody[] = {
    {  784,  234}, {    0,   15}, {  740,  231}, {    0,   15},
    {  659,  478}, {    0,   17}, {  740,  513}, {  370,  262},
    {  294,  249}, {  247,  234}, {  294,  247}, {  370,  262},
    {  294,  248}, {  220,  252}, {  294,  248}, {  370,  247},
    {  440,  234}, {  294,   15}, {  784,  234}, {  247,   15},
    {  740,  232}, {  294,   15}, {  659,  746}, {  220,   15},
    {  740,  233}, {  294,   15}, {  370,  262}, {  294,  234},
    {  587,  495}, {  659,  232}, {  370,   15}, {  440,  233},
    {  294,   30}, {  220,  252}, {  294,  248}, {  370,  262},
    {  294,  249}, {  247,  234}, {  294,  247}, {  370,  247},
    {  440,  233}, {  294,   17}, {  659,  513}, {  740,  232},
    {  392,   15}, {  784,  744}, {  659,  232}, {  392,   15},
    {  554,  500}, {  220,   15}, {  587,  744}, {  659,  495},
    {  440,  761}, {  740,  494}, {  370,   15}, {  294,  249},
    {  247,  234}, {  294,  247}, {  370,  262}, {  294,  248},
    {  220,  252}, {  294,  248}, {  370,  262}, {  294,  234},
    {  784,  234}, {  247,   15}, {  740,  232}, {  294,   15},
    {  659,  479}, {  294,   17}, {  740,  513}, {  370,  262},
    {  294,  249}, {  247,  234}, {  294,  247}, {  370,  262},
    {  294,  248}, {  220,  252}, {  294,  248}, {  370,  247},
    {  440,  234}, {  294,   15}, {  784,  234}, {  247,   15},
    {  740,  232}, {  294,   15}, {  659,  479}, {  294,   30},
    {  220,  252}, {  294,  248}, {  740,  232}, {  370,   15},
    {  587,  234}, {  294,   30}, {  247,  234}, {  294,  247},
    {  659,  232}, {  370,   15}, {  440,  233}, {  294,   30},
    {  220,  252}, {  294,  248}, {  370,  262}, {  294,  249},
    {  247,  234}, {  294,  247}, {  370,  262}, {  294,  235},
    {  659,  513}, {  740,  232}, {  392,   15}, {  784,  744},
    {  659,  232}, {  392,   15}, {  554,  763}, {  587,  232},
    {  392,   15}, {  659,  234}, {  330,   30}, {  220,  234},
    {  440,  232}, {  330,   15}, {  587,  232}, {  392,   15},
    {  659,  233}, {  330,   17}, {  698,  250}, {  233,   15},
    {  659,  233}, {  294,   15}, {  587,  232}, {  349,   15},
    {  523,  234}, {  466,   30}, {    0,  481}, {  440,  231},
    {    0,   15}, {  466,  232}, {    0,   17}, {  523,  513},
    {  698,  496}, {  659,  234}, {  196,   15}, {  587,  478},
    {  349,   15}, {  523,  233}, {  294,   17}, {  587,  250},
    {  220,   15}, {  523, 1245}, {  262,    1}, {  440,  238},
    {  349,   16}, {  466,  241}, {  262,   18}, {  523,  517},
    {  698,  496}, {  784,  234}, {  196,   15}, {  698,  232},
    {  294,   15}, {  659,  232}, {  349,   15}, {  587,  247},
    {  294,    2}, {  587,  250}, {  220,   15}, {  659,  233},
    {  262,   15}, {  698,  991}, {  784,  232}, {  349,   15},
    {  880,  233}, {  262,   17}, {  932,  497}, {  349,   15},
    {  880,  496}, {  784,  495}, {  698,  232}, {  440,   15},
    {  784,  233}, {  349,   17}, {  880,  497}, {  262,   15},
    {  784,  496}, {  698,  495}, {  587,  232}, {  349,   15},
    {  523,  233}, {  262,   17}, {  587,  250}, {  220,   15},
    {  698,  480}, {  349,   15}, {  659,  730}, {  262,   15},
    {  740, 1270}, {  587,  249}, {  494,  234}, {  587,  247},
    {  740,  261}, {  587,  248}, {  440,  252}, {  587,  248},
    {  740,  261}, {  587,  249}, {  494,  234}, {  587,  247},
    {  740,  261}, {  587,  248}, {  554,  252}, {  659,  248},
    {  740,  996}, {  370,  262}, {  294,  249}, {  247,  234},
    {  294,  247}, {  370,  262}, {  294,  248}, {  220,  252},
    {  294,  248}, {  370,  262}, {  294,  234}, {  784,  234},
    {  247,   15}, {  740,  232}, {  294,   15}, {  659,  479},
    {  294,   17}, {  740,  513}, {  370,  262}, {  294,  249},
    {  247,  234}, {  294,  247}, {  370,  262}, {  294,  248},
    {  220,  252}, {  294,  248}, {  370,  247}, {  440,  234},
    {  294,   15}, {  784,  234}, {  247,   15}, {  740,  232},
    {  294,   15}, {  659,  479}, {  294,   30}, {  220,  252},
    {  294,  248}, {  740,  232}, {  370,   15}, {  587,  234},
    {  294,   30}, {  247,  234}, {  294,  247}, {  659,  232},
    {  370,   15}, {  440,  233}, {  294,   30}, {  220,  252},
    {  294,  248}, {  370,  262}, {  294,  249}, {  247,  234},
    {  294,  247}, {  370,  262}, {  294,  235}, {  659,  513},
    {  740,  232}, {  392,   15}, {  784,  744}, {  659,  232},
    {  392,   15}, {  554,  500}, {  220,   15}, {  587,  744},
    {  659,  495}, {  440,  507}, {  698,  250}, {  880,  247},
    {  932,  497}, {  880,  496}, {  784,  496}, {  698,  232},
    {  440,   15}, {  784,  233}, {  349,   17}, {  880,  497},
    {  262,   15}, {  784,  496}, {  698,  495}, {  587,  232},
    {  349,   15}, {  523,  233}, {  262,   17}, {  587,  250},
    {  220,   15}, {  698,  480}, {  349,   15}, {  659,  730},
    {  262,   15}, {  740, 1270}, {  587,  249}, {  494,  234},
    {  587,  247}, {  740,  261}, {  587,  248}, {  440,  252},
    {  587,  248}, {  740,  261}, {  587,  249}, {  494,  234},
    {  587,  247}, {  740,  261}, {  587,  248}, {  554,  252},
    {  659,  248}, {  740,  996}, {  370,  262}, {  294,  249},
    {  247,  234}, {  880,  725}, {  294,   17}, {  988,  250},
    {  220,   15}, {  880,  233}, {  294,   15}, {  740,  232},
    {  370,   15}, {  587,  498}, {  659,  247}, {  740, 1008},
    {  370,  262}, {  294,  249}, {  247,  234}, {  880,  725},
    {  294,   17}, {  988,  250}, {  220,   15}, {  880,  233},
    {  294,   15}, {  740,  232}, {  370,   15}, {  587,  498},
    {  659,  247}, {  740, 1008}, {  370,  262}, {  294,  249},
    {  247,  234}, {  294,  247}, {  880,  479}, {  294,   17},
    {  988,  250}, {  220,   15}, {  880,  233}, {  294,   15},
    {  740,  232}, {  370,   15}, {  587,  498}, {  659,  247},
    {  740, 1008}, {  370,  262}, {  294,  249}, {  247,  234},
    {  880,  725}, {  294,   17}, {  988,  250}, {  220,   15},
    {  880,  233}, {  294,   15}, {  740,  232}, {  370,   15},
    {  587,  498}, {  659,  247}, {  740, 1008}, {  370,  262},
    {  294,  249}, {  247,  234}, {  784,  232}, {  185,   15},
    {  740, 1008}
};

#define NOTE_COUNT ((int)(sizeof(still_alive_melody)/sizeof(still_alive_melody[0])))

/* =========================================================================
 * Combined playback: music + lyrics + animated art
 *
 * Lyric chars spread evenly: output char lc when
 *   note_index >= lc * NOTE_COUNT / lc_total
 *
 * Art schedule: switch image when note index >= art_schedule[next].note_idx
 * ========================================================================= */
static void play_with_lyrics(void) {
    int lc       = 0;   /* next lyric char to output */
    int art_next = 0;   /* next entry in art_schedule to check */

    for (int i = 0; i < NOTE_COUNT; i++) {

        /* ── Art image switch ── */
        while (art_next < ART_SCHED_COUNT &&
               i >= art_schedule[art_next].note_idx) {
            sa_draw_art(art_schedule[art_next].art_id);
            art_next++;
        }

        /* ── Lyric typewriter ── */
        while (lc < lc_total &&
               (lc * NOTE_COUNT / lc_total) <= i) {
            vga_set_cursor(lc_buf[lc].row, lc_buf[lc].col);
            vga_putchar(lc_buf[lc].ch);
            lc++;
        }

        /* ── Play note ── */
        int ms = still_alive_melody[i].ms;
        if (ms < 1) ms = 1;
        if (still_alive_melody[i].freq == 0)
            speaker_off();
        else
            speaker_on(still_alive_melody[i].freq);
        timer_sleep((uint32_t)ms);
    }

    speaker_off();

    /* flush any remaining lyric chars (rounding artefacts) */
    while (lc < lc_total) {
        vga_set_cursor(lc_buf[lc].row, lc_buf[lc].col);
        vga_putchar(lc_buf[lc].ch);
        lc++;
    }
}

/* =========================================================================
 * Public entry point
 * ========================================================================= */
void stillalive_play(void) {
    /* 1 — clear to solid black using amber attribute */
    sa_color();
    vga_clear();

    /* 2 — frame and static right column (names + separator) appear instantly */
    sa_draw_frame();
    sa_draw_names();

    /* 3 — initial art (Aperture Science logo) */
    sa_draw_art(0);

    /* 4 — pre-compute lyric char positions */
    build_lyric_chars();

    /* 5 — play music while typing lyrics and switching art in sync */
    play_with_lyrics();

    /* 6 — finished; restore normal colour */
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}
