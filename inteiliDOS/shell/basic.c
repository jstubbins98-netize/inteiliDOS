/*
 * inteilidOS -- shell/basic.c
 * InteiliBASIC v1.0  --  A complete integer BASIC interpreter
 *
 * Variables  : A-Z (int32_t)  A$-Z$ (string <=79 chars)  A(n)-Z(n) (array)
 *
 * Statements : PRINT  INPUT  LET  IF/THEN/ELSE  GOTO  GOSUB  RETURN
 *              FOR/NEXT  REM  DATA  READ  RESTORE  DIM  END  STOP  CLS
 *
 * Operators  : + - * / MOD   = <> < > <= >=   AND OR NOT
 *
 * Numeric fn : INT  ABS  SGN  RND  LEN  VAL  ASC
 * String fn  : CHR$  STR$  LEFT$  RIGHT$  MID$  INKEY$
 *
 * REPL cmds  : RUN  LIST  NEW  SAVE  LOAD  HELP  BYE  QUIT
 */

#include "basic.h"
#include "../kernel/vga.h"
#include "../kernel/keyboard.h"
#include "../kernel/memory.h"
#include <stdint.h>

/* ================================================================
 * Constants
 * ================================================================ */
#define PROG_MAX    256       /* max program lines                  */
#define LINE_MAX    128       /* max chars per source line          */
#define STR_MAX     79        /* max chars in a string value        */
#define GOSUB_MAX   16        /* GOSUB call stack depth             */
#define FOR_MAX     8         /* FOR nesting depth                  */
#define ARRAY_MAX   64        /* max elements per 1-D array         */
#define DATA_MAX    256       /* max total DATA items               */

/* ================================================================
 * Types
 * ================================================================ */
typedef struct {
    int  linenum;
    char text[LINE_MAX];
} prog_line_t;

typedef struct {
    int     varidx;    /* 0-25 = A-Z                     */
    int32_t limit;
    int32_t step;
    int     body_pc;   /* prog[] index of first body line */
} for_frame_t;

/* ================================================================
 * Global state
 * ================================================================ */
static prog_line_t g_prog[PROG_MAX];
static int         g_prog_size;

/* Persistent between runs */
static int32_t     g_num[26];                /* A-Z       */
static char        g_str[26][STR_MAX + 1];   /* A$-Z$     */
static int32_t     g_arr[26][ARRAY_MAX];     /* arrays    */
static int         g_arr_dim[26];

/* Runtime */
static int         g_pc;           /* current prog[] index         */
static int         g_running;
static int         g_had_error;
static int         g_jumped;       /* set when g_pc changed manually */

static int         g_gosub[GOSUB_MAX];
static int         g_gosub_sp;

static for_frame_t g_for[FOR_MAX];
static int         g_for_sp;

/* DATA / READ */
static char        g_data[DATA_MAX][STR_MAX + 1];
static int         g_data_cnt;
static int         g_data_ptr;

/* SAVE / LOAD bank */
static prog_line_t g_saved[PROG_MAX];
static int         g_saved_size;

/* RNG */
static uint32_t    g_rnd = 12345;

/* Parse cursor (set before each statement) */
static const char *g_p;

/* Print column tracker */
static int         g_col;

/* ================================================================
 * Inline char helpers  (no stdlib)
 * ================================================================ */
static inline int bu(int c)   { return (c>='a'&&c<='z') ? c-32 : c; }
static inline int bisa(int c) { return (c>='A'&&c<='Z')||(c>='a'&&c<='z'); }
static inline int bisd(int c) { return c>='0' && c<='9'; }
static inline int bisan(int c){ return bisa(c)||bisd(c); }

/* ================================================================
 * Output helpers  (track column so comma-PRINT tabs work)
 * ================================================================ */
static void bputc(char c) {
    vga_putchar(c);
    if (c == '\n') g_col = 0; else g_col++;
}
static void bputs(const char *s) { while (*s) bputc(*s++); }

static void bprint_uint(uint32_t n) {
    char buf[11]; int i = 0;
    if (n == 0) { bputc('0'); return; }
    while (n) { buf[i++] = (char)('0' + n % 10); n /= 10; }
    while (i > 0) bputc(buf[--i]);
}
static void bprint_int(int32_t n) {
    if (n < 0) { bputc('-'); bprint_uint((uint32_t)(-(n+1)) + 1u); }
    else { bputc(' '); bprint_uint((uint32_t)n); }
}

static void bprint_tab(void) {
    int next = ((g_col / 14) + 1) * 14;
    while (g_col < next && g_col < 78) bputc(' ');
    if (g_col >= 78) bputc('\n');
}

/* int32 → string */
static void int_to_str(int32_t n, char *out, int maxlen) {
    char tmp[12]; int i = 0;
    int neg = (n < 0);
    uint32_t u = neg ? (uint32_t)(-(n+1))+1u : (uint32_t)n;
    if (u == 0) { kstrncpy(out, "0", maxlen); return; }
    while (u) { tmp[i++] = (char)('0' + u % 10); u /= 10; }
    if (neg) tmp[i++] = '-';
    int j = 0;
    while (i > 0 && j < maxlen - 1) out[j++] = tmp[--i];
    out[j] = '\0';
}

/* string → int32 */
static int32_t str_to_int(const char *s) {
    while (*s == ' ') s++;
    int neg = 0;
    if (*s == '-') { neg=1; s++; }
    else if (*s == '+') s++;
    int32_t v = 0;
    while (bisd(*s)) { v = v*10 + (*s-'0'); s++; }
    return neg ? -v : v;
}

/* ================================================================
 * Error handling
 * ================================================================ */
static void basic_error(const char *msg) {
    if (g_had_error) return;
    g_had_error = 1;
    g_running   = 0;
    vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    bputc('\n'); bputs("?"); bputs(msg);
    bputs(" error");
    if (g_pc >= 0 && g_pc < g_prog_size) {
        bputs(" in line ");
        bprint_uint((uint32_t)g_prog[g_pc].linenum);
    }
    bputc('\n');
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

/* ================================================================
 * Line input  (for INPUT statement and REPL)
 * ================================================================ */
static void basic_readline(char *buf, int max) {
    int n = 0;
    for (;;) {
        int c = keyboard_getchar();
        if (c == '\r' || c == '\n') { bputc('\n'); break; }
        if ((c == '\b' || c == 0x08) && n > 0) {
            n--; bputc('\b'); bputc(' '); bputc('\b');
        } else if (c >= 0x20 && c < 0x7F && n < max - 1) {
            buf[n++] = (char)c; bputc((char)c);
        }
    }
    buf[n] = '\0';
}

/* ================================================================
 * Parse helpers
 * ================================================================ */
static void skip_ws(void) { while (*g_p==' '||*g_p=='\t') g_p++; }

/* Match keyword at g_p (case-insensitive, must not be followed by alnum/$) */
static int match_kw(const char *kw) {
    const char *p = g_p;
    while (*kw) { if (bu(*p) != bu(*kw)) return 0; p++; kw++; }
    if (bisan(*p) || *p == '$') return 0;
    g_p = p;
    return 1;
}

static void expect_char(char c) {
    skip_ws();
    if (*g_p == c) { g_p++; return; }
    basic_error("Syntax");
}

/* Parse a positive integer literal at g_p (or return -1) */
static int parse_lineno(void) {
    skip_ws();
    if (!bisd(*g_p)) return -1;
    int n = 0;
    while (bisd(*g_p)) n = n*10 + (*g_p++ - '0');
    return n;
}

/* ================================================================
 * Program management
 * ================================================================ */
static int find_line(int lineno) {
    for (int i = 0; i < g_prog_size; i++)
        if (g_prog[i].linenum == lineno) return i;
    return -1;
}

static int find_line_ge(int lineno) {
    /* First line with linenum >= lineno */
    for (int i = 0; i < g_prog_size; i++)
        if (g_prog[i].linenum >= lineno) return i;
    return g_prog_size;
}

static void insert_line(int lineno, const char *text) {
    int idx = find_line(lineno);
    if (idx >= 0) {
        /* Replace */
        kstrncpy(g_prog[idx].text, text, LINE_MAX - 1);
        g_prog[idx].text[LINE_MAX - 1] = '\0';
        return;
    }
    if (g_prog_size >= PROG_MAX) { bputs("?Out of program space\n"); return; }
    /* Insert in sorted order */
    idx = find_line_ge(lineno);
    for (int i = g_prog_size; i > idx; i--) g_prog[i] = g_prog[i-1];
    g_prog[idx].linenum = lineno;
    kstrncpy(g_prog[idx].text, text, LINE_MAX - 1);
    g_prog[idx].text[LINE_MAX - 1] = '\0';
    g_prog_size++;
}

static void delete_line(int lineno) {
    int idx = find_line(lineno);
    if (idx < 0) return;
    for (int i = idx; i < g_prog_size - 1; i++) g_prog[i] = g_prog[i+1];
    g_prog_size--;
}

static void do_new(void) {
    g_prog_size = 0;
    for (int i = 0; i < 26; i++) {
        g_num[i] = 0;
        g_str[i][0] = '\0';
        g_arr_dim[i] = 0;
    }
    g_data_cnt = g_data_ptr = 0;
    g_gosub_sp = g_for_sp = 0;
    g_col = 0;
}

/* ================================================================
 * DATA pre-scan  (called before every RUN)
 * ================================================================ */
static void collect_data(void) {
    g_data_cnt = 0; g_data_ptr = 0;
    for (int li = 0; li < g_prog_size; li++) {
        const char *p = g_prog[li].text;
        /* skip spaces */
        while (*p == ' ') p++;
        /* check for DATA keyword */
        const char *q = p;
        if (bu(q[0])!='D'||bu(q[1])!='A'||bu(q[2])!='T'||bu(q[3])!='A') continue;
        if (bisan(q[4])) continue;
        p += 4;
        /* parse comma-separated items */
        for (;;) {
            while (*p == ' ') p++;
            if (!*p || *p == ':' || *p == '\'') break;
            int j = 0;
            if (*p == '"') {
                p++;
                while (*p && *p != '"' && j < STR_MAX)
                    g_data[g_data_cnt][j++] = *p++;
                if (*p == '"') p++;
            } else {
                while (*p && *p != ',' && *p != ':' && j < STR_MAX)
                    g_data[g_data_cnt][j++] = *p++;
                /* trim trailing spaces */
                while (j > 0 && g_data[g_data_cnt][j-1] == ' ') j--;
            }
            g_data[g_data_cnt][j] = '\0';
            if (g_data_cnt < DATA_MAX - 1) g_data_cnt++;
            while (*p == ' ') p++;
            if (*p == ',') p++; else break;
        }
    }
}

/* ================================================================
 * Forward declarations
 * ================================================================ */
static int32_t eval_num(void);
static void    eval_str(char *out, int maxlen);

/* ================================================================
 * String expression evaluator
 * ================================================================ */
static void eval_str_term(char *out, int maxlen) {
    skip_ws();
    out[0] = '\0';

    /* String literal */
    if (*g_p == '"') {
        g_p++; int j = 0;
        while (*g_p && *g_p != '"' && j < maxlen-1) out[j++] = *g_p++;
        out[j] = '\0';
        if (*g_p == '"') g_p++;
        return;
    }

    /* Identifier: variable or function */
    if (bisa(*g_p)) {
        char name[8]; int ni = 0;
        while (bisa(*g_p) && ni < 7) name[ni++] = (char)bu(*g_p++);
        name[ni] = '\0';
        int has_dollar = (*g_p == '$');
        if (has_dollar) g_p++;

        /* ---- String variables: single letter + $ ---- */
        if (ni == 1 && has_dollar) {
            skip_ws();
            int idx = name[0] - 'A';
            if (*g_p == '(') {
                /* Array of strings not supported; treat as variable */
            }
            kstrncpy(out, g_str[idx], maxlen - 1);
            out[maxlen - 1] = '\0';
            return;
        }

        /* ---- String functions ---- */
        if (kstrcmp(name, "CHR") == 0 && has_dollar) {
            expect_char('('); int32_t n = eval_num(); expect_char(')');
            if (!g_had_error) { out[0]=(char)(n&0x7F); out[1]='\0'; }
            return;
        }
        if (kstrcmp(name, "STR") == 0 && has_dollar) {
            expect_char('('); int32_t n = eval_num(); expect_char(')');
            if (!g_had_error) int_to_str(n, out, maxlen);
            return;
        }
        if (kstrcmp(name, "LEFT") == 0 && has_dollar) {
            char s[STR_MAX+1];
            expect_char('('); eval_str(s, STR_MAX); expect_char(',');
            int32_t n = eval_num(); expect_char(')');
            if (g_had_error) return;
            int sl = (int)kstrlen(s);
            if (n > sl) n = sl; if (n < 0) n = 0; if (n >= maxlen) n = maxlen-1;
            kmemcpy(out, s, (size_t)n); out[n] = '\0';
            return;
        }
        if (kstrcmp(name, "RIGHT") == 0 && has_dollar) {
            char s[STR_MAX+1];
            expect_char('('); eval_str(s, STR_MAX); expect_char(',');
            int32_t n = eval_num(); expect_char(')');
            if (g_had_error) return;
            int sl = (int)kstrlen(s);
            if (n > sl) n = sl; if (n < 0) n = 0;
            int start = sl - (int)n;
            kstrncpy(out, s + start, maxlen - 1); out[maxlen-1] = '\0';
            return;
        }
        if (kstrcmp(name, "MID") == 0 && has_dollar) {
            char s[STR_MAX+1];
            expect_char('('); eval_str(s, STR_MAX); expect_char(',');
            int32_t pos = eval_num();
            int32_t len = -1;
            skip_ws();
            if (*g_p == ',') { g_p++; len = eval_num(); }
            expect_char(')');
            if (g_had_error) return;
            int sl = (int)kstrlen(s);
            int start = (int)pos - 1; if (start < 0) start = 0;
            if (start >= sl) { out[0] = '\0'; return; }
            int avail = sl - start;
            if (len < 0 || len > avail) len = avail;
            if (len >= maxlen) len = maxlen - 1;
            kmemcpy(out, s + start, (size_t)len); out[len] = '\0';
            return;
        }
        if (kstrcmp(name, "INKEY") == 0 && has_dollar) {
            int k = keyboard_poll();
            if (k >= 0x20 && k < 0x7F) { out[0]=(char)k; out[1]='\0'; }
            else out[0] = '\0';
            return;
        }
        if (kstrcmp(name, "INPUT") == 0 && has_dollar) {
            expect_char('('); int32_t n = eval_num(); expect_char(')');
            if (g_had_error) return;
            if (n < 0) n = 0; if (n >= maxlen) n = maxlen-1;
            for (int i = 0; i < (int)n; i++) {
                int c = keyboard_getchar();
                if (c < 0x20 || c >= 0x7F) { out[i]='\0'; return; }
                out[i] = (char)c;
            }
            out[n] = '\0';
            return;
        }
        if (kstrcmp(name, "SPACE") == 0 && has_dollar) {
            expect_char('('); int32_t n = eval_num(); expect_char(')');
            if (g_had_error) return;
            if (n < 0) n = 0; if (n >= maxlen) n = maxlen-1;
            for (int i = 0; i < (int)n; i++) out[i] = ' ';
            out[n] = '\0';
            return;
        }
        if (kstrcmp(name, "STRING") == 0 && has_dollar) {
            expect_char('('); int32_t n = eval_num(); expect_char(',');
            char s2[STR_MAX+1]; eval_str(s2, STR_MAX); expect_char(')');
            if (g_had_error) return;
            char fill = s2[0] ? s2[0] : ' ';
            if (n < 0) n = 0; if (n >= maxlen) n = maxlen-1;
            for (int i = 0; i < (int)n; i++) out[i] = fill;
            out[n] = '\0';
            return;
        }

        basic_error("Unknown string function");
        out[0] = '\0';
    }
}

static void eval_str(char *out, int maxlen) {
    eval_str_term(out, maxlen);
    for (;;) {
        skip_ws();
        if (*g_p != '+') break;
        /* Peek: is next token also string? */
        const char *save = g_p;
        g_p++;
        skip_ws();
        if (*g_p == '"' || (bisa(*g_p) &&
            /* Check if identifier ends in $ */
            (*(g_p+1) == '$' || (bisa(*(g_p+1)) && *(g_p+2) == '$') ||
             (bisa(*(g_p+1)) && bisa(*(g_p+2)) && *(g_p+3) == '$') ||
             (bisa(*(g_p+1)) && bisa(*(g_p+2)) && bisa(*(g_p+3)) && *(g_p+4) == '$') ||
             (bisa(*(g_p+1)) && bisa(*(g_p+2)) && bisa(*(g_p+3)) && bisa(*(g_p+4)) && *(g_p+5) == '$')))) {
            /* String concatenation */
            char tmp[STR_MAX+1];
            eval_str_term(tmp, STR_MAX);
            if (g_had_error) return;
            int olen = (int)kstrlen(out);
            int tlen = (int)kstrlen(tmp);
            if (olen + tlen < maxlen)
                kmemcpy(out + olen, tmp, (size_t)(tlen + 1));
        } else {
            g_p = save; /* not string concat, put '+' back */
            break;
        }
    }
}

/* ================================================================
 * Numeric expression evaluator  (recursive descent)
 * ================================================================ */
static int32_t eval_primary(void);
static int32_t eval_unary(void);
static int32_t eval_mul(void);
static int32_t eval_add(void);
static int32_t eval_cmp(void);
static int32_t eval_not(void);
static int32_t eval_and(void);

static int32_t eval_num(void) { /* = eval_or */
    int32_t a = eval_and();
    for (;;) {
        skip_ws();
        if (!match_kw("OR")) break;
        int32_t b = eval_and();
        a = (a || b) ? -1 : 0;
    }
    return a;
}
static int32_t eval_and(void) {
    int32_t a = eval_not();
    for (;;) {
        skip_ws();
        if (!match_kw("AND")) break;
        int32_t b = eval_not();
        a = (a && b) ? -1 : 0;
    }
    return a;
}
static int32_t eval_not(void) {
    skip_ws();
    if (match_kw("NOT")) return eval_cmp() ? 0 : -1;
    return eval_cmp();
}
static int32_t eval_cmp(void) {
    int32_t a = eval_add();
    for (;;) {
        skip_ws();
        int op = 0;
        /* Order matters: check 2-char ops first */
        if (g_p[0]=='<'&&g_p[1]=='>') { op=1; g_p+=2; }
        else if (g_p[0]=='<'&&g_p[1]=='=') { op=2; g_p+=2; }
        else if (g_p[0]=='>'&&g_p[1]=='=') { op=3; g_p+=2; }
        else if (*g_p=='<'&&g_p[1]!='<') { op=4; g_p++; }
        else if (*g_p=='>'&&g_p[1]!='>') { op=5; g_p++; }
        else if (*g_p=='='&&g_p[1]!='=') { op=6; g_p++; }
        else break;
        int32_t b = eval_add();
        switch (op) {
            case 1: a = (a!=b)?-1:0; break;
            case 2: a = (a<=b)?-1:0; break;
            case 3: a = (a>=b)?-1:0; break;
            case 4: a = (a< b)?-1:0; break;
            case 5: a = (a> b)?-1:0; break;
            case 6: a = (a==b)?-1:0; break;
        }
    }
    return a;
}
static int32_t eval_add(void) {
    int32_t a = eval_mul();
    for (;;) {
        skip_ws();
        if (*g_p == '+') { g_p++; a += eval_mul(); }
        else if (*g_p == '-') { g_p++; a -= eval_mul(); }
        else break;
    }
    return a;
}
static int32_t eval_mul(void) {
    int32_t a = eval_unary();
    for (;;) {
        skip_ws();
        if (*g_p == '*') { g_p++; a *= eval_unary(); }
        else if (*g_p == '/') {
            g_p++; int32_t b = eval_unary();
            if (b == 0) { basic_error("Division by zero"); return 0; }
            a /= b;
        }
        else if (*g_p == '\\') {
            g_p++; int32_t b = eval_unary();
            if (b == 0) { basic_error("Division by zero"); return 0; }
            a /= b;
        }
        else if (match_kw("MOD")) {
            int32_t b = eval_unary();
            if (b == 0) { basic_error("Modulo by zero"); return 0; }
            a %= b;
        }
        else break;
    }
    return a;
}
static int32_t eval_unary(void) {
    skip_ws();
    if (*g_p == '-') { g_p++; return -eval_primary(); }
    if (*g_p == '+') { g_p++; return  eval_primary(); }
    return eval_primary();
}

static int32_t eval_primary(void) {
    skip_ws();
    if (g_had_error) return 0;

    /* Parenthesised expression */
    if (*g_p == '(') {
        g_p++; int32_t v = eval_num(); skip_ws(); expect_char(')');
        return v;
    }

    /* Numeric literal */
    if (bisd(*g_p)) {
        int32_t v = 0;
        while (bisd(*g_p)) v = v*10 + (*g_p++ - '0');
        return v;
    }

    /* Hex literal  &H... */
    if (*g_p == '&' && (bu(*(g_p+1)) == 'H')) {
        g_p += 2; uint32_t v = 0;
        while (1) {
            char c = (char)bu(*g_p);
            if (c >= '0' && c <= '9') v = v*16 + (uint32_t)(c-'0');
            else if (c >= 'A' && c <= 'F') v = v*16 + (uint32_t)(c-'A'+10);
            else break;
            g_p++;
        }
        return (int32_t)v;
    }

    /* Identifier: function or variable */
    if (bisa(*g_p)) {
        char name[8]; int ni = 0;
        while (bisa(*g_p) && ni < 7) name[ni++] = (char)bu(*g_p++);
        name[ni] = '\0';
        int has_dollar = (*g_p == '$');
        if (has_dollar) g_p++;

        /* ---------- numeric functions ---------- */
        if (!has_dollar) {
            if (kstrcmp(name,"ABS")==0) {
                expect_char('('); int32_t v=eval_num(); expect_char(')');
                return v<0?-v:v;
            }
            if (kstrcmp(name,"INT")==0) { /* integers only, INT=identity */
                expect_char('('); int32_t v=eval_num(); expect_char(')');
                return v;
            }
            if (kstrcmp(name,"SGN")==0) {
                expect_char('('); int32_t v=eval_num(); expect_char(')');
                return v>0?1:(v<0?-1:0);
            }
            if (kstrcmp(name,"RND")==0) {
                expect_char('('); int32_t n=eval_num(); expect_char(')');
                g_rnd = g_rnd*1664525u + 1013904223u;
                if (n <= 0) return 0;
                return (int32_t)((g_rnd >> 1) % (uint32_t)n) + 1;
            }
            if (kstrcmp(name,"LEN")==0) {
                expect_char('(');
                char s[STR_MAX+1]; eval_str(s, STR_MAX);
                expect_char(')');
                return (int32_t)kstrlen(s);
            }
            if (kstrcmp(name,"VAL")==0) {
                expect_char('(');
                char s[STR_MAX+1]; eval_str(s, STR_MAX);
                expect_char(')');
                return str_to_int(s);
            }
            if (kstrcmp(name,"ASC")==0) {
                expect_char('(');
                char s[STR_MAX+1]; eval_str(s, STR_MAX);
                expect_char(')');
                return s[0] ? (uint8_t)s[0] : 0;
            }
            if (kstrcmp(name,"POS")==0) {
                expect_char('('); eval_num(); expect_char(')'); /* consume arg */
                return (int32_t)g_col;
            }
            if (kstrcmp(name,"FRE")==0) {
                expect_char('('); eval_num(); expect_char(')');
                return 131072; /* pretend 128 KB free */
            }
            if (kstrcmp(name,"PEEK")==0) {
                expect_char('('); int32_t addr=eval_num(); expect_char(')');
                return *(volatile uint8_t *)(uintptr_t)(uint32_t)addr;
            }

            /* Single-letter variable */
            if (ni == 1) {
                int idx = name[0] - 'A';
                skip_ws();
                if (*g_p == '(') {
                    g_p++;
                    int32_t i = eval_num(); expect_char(')');
                    if (g_had_error) return 0;
                    if (i < 1 || i > g_arr_dim[idx]) { basic_error("Array bounds"); return 0; }
                    return g_arr[idx][(int)i - 1];
                }
                return g_num[idx];
            }

            basic_error("Unknown function");
            return 0;
        }

        /* ---------- string-to-number functions ---------- */
        /* e.g. someone writes LEN(A$) is handled above; here we have
           identifiers ending in $ that are NOT valid in numeric context */
        if (ni == 1 && has_dollar) {
            basic_error("Type mismatch");
            return 0;
        }
        basic_error("Type mismatch");
        return 0;
    }

    basic_error("Expected expression");
    return 0;
}

/* ================================================================
 * String-comparison helper
 * ================================================================ */
static int eval_str_cmp(void) {
    /* Returns: -1 if a<b, 0 if a=b, 1 if a>b  (not used yet) */
    char a[STR_MAX+1], b[STR_MAX+1];
    eval_str(a, STR_MAX);
    skip_ws();
    int op = 0;
    if (g_p[0]=='<'&&g_p[1]=='>') { op=1; g_p+=2; }
    else if (g_p[0]=='<'&&g_p[1]=='=') { op=2; g_p+=2; }
    else if (g_p[0]=='>'&&g_p[1]=='=') { op=3; g_p+=2; }
    else if (*g_p=='<') { op=4; g_p++; }
    else if (*g_p=='>') { op=5; g_p++; }
    else if (*g_p=='=') { op=6; g_p++; }
    eval_str(b, STR_MAX);
    int c = kstrcmp(a, b);
    switch (op) {
        case 1: return (c!=0)?-1:0;
        case 2: return (c<=0)?-1:0;
        case 3: return (c>=0)?-1:0;
        case 4: return (c< 0)?-1:0;
        case 5: return (c> 0)?-1:0;
        case 6: return (c==0)?-1:0;
    }
    return 0;
}

/* ================================================================
 * Helpers: is next expression string-typed?
 * ================================================================ */
static int is_str_ahead(void) {
    const char *sv = g_p;
    skip_ws();
    if (*g_p == '"') { g_p = sv; return 1; }
    if (bisa(*g_p)) {
        const char *p = g_p;
        while (bisa(*p)) p++;
        if (*p == '$') { g_p = sv; return 1; }
    }
    g_p = sv;
    return 0;
}

/* ================================================================
 * GOTO helper
 * ================================================================ */
static void do_goto(int lineno) {
    int idx = find_line(lineno);
    if (idx < 0) { basic_error("Undefined line"); return; }
    g_pc = idx;
    g_jumped = 1;
}

/* ================================================================
 * Statement executors
 * ================================================================ */

/* PRINT [TAB(n)|SPC(n)|expr] [;|,] ... */
static void exec_print(void) {
    skip_ws();
    if (!*g_p || *g_p == ':' || *g_p == '\'') { bputc('\n'); return; }

    int need_nl = 1;
    for (;;) {
        skip_ws();
        if (!*g_p || *g_p == ':' || *g_p == '\'') break;

        /* TAB(n) */
        if (match_kw("TAB")) {
            expect_char('('); int32_t n = eval_num(); expect_char(')');
            if (!g_had_error) while (g_col < (int)n && g_col < 78) bputc(' ');
            goto sep;
        }
        /* SPC(n) */
        if (match_kw("SPC")) {
            expect_char('('); int32_t n = eval_num(); expect_char(')');
            if (!g_had_error) for (int i=0;i<(int)n&&g_col<78;i++) bputc(' ');
            goto sep;
        }

        if (is_str_ahead()) {
            char s[STR_MAX+1]; eval_str(s, STR_MAX);
            if (!g_had_error) bputs(s);
        } else {
            int32_t v = eval_num();
            if (!g_had_error) { bprint_int(v); bputc(' '); }
        }
        if (g_had_error) return;

    sep:
        skip_ws();
        need_nl = 1;
        if (*g_p == ';') { g_p++; need_nl = 0; continue; }
        if (*g_p == ',') { g_p++; bprint_tab(); need_nl = 0; continue; }
        break;
    }
    if (need_nl) bputc('\n');
}

/* LET var = expr  (also handles implicit assignment) */
static void exec_let(void) {
    skip_ws();
    if (!bisa(*g_p)) { basic_error("Syntax"); return; }
    int idx = bu(*g_p) - 'A'; g_p++;
    int is_str_var = (*g_p == '$');
    if (is_str_var) g_p++;

    int is_arr = 0; int32_t arr_idx = 0;
    if (!is_str_var && *g_p == '(') {
        is_arr = 1; g_p++;
        arr_idx = eval_num(); expect_char(')');
        if (g_had_error) return;
        if (arr_idx < 1 || arr_idx > g_arr_dim[idx]) { basic_error("Array bounds"); return; }
    }

    skip_ws(); expect_char('=');
    if (g_had_error) return;

    if (is_str_var) {
        eval_str(g_str[idx], STR_MAX);
    } else if (is_arr) {
        g_arr[idx][(int)arr_idx - 1] = eval_num();
    } else {
        g_num[idx] = eval_num();
    }
}

/* INPUT ["prompt";] var [, var ...] */
static void exec_input(void) {
    /* Optional prompt */
    skip_ws();
    if (*g_p == '"') {
        g_p++;
        while (*g_p && *g_p != '"') bputc(*g_p++);
        if (*g_p == '"') g_p++;
        skip_ws();
        if (*g_p == ';' || *g_p == ',') g_p++;
    } else {
        bputs("? ");
    }

    char linebuf[LINE_MAX];

    for (;;) {
        skip_ws();
        if (!bisa(*g_p)) break;

        int idx = bu(*g_p) - 'A'; g_p++;
        int is_sv = (*g_p == '$');
        if (is_sv) g_p++;

        int is_arr = 0; int32_t ai = 0;
        if (!is_sv && *g_p == '(') {
            is_arr = 1; g_p++;
            ai = eval_num(); expect_char(')');
            if (g_had_error) return;
            if (ai < 1 || ai > g_arr_dim[idx]) { basic_error("Array bounds"); return; }
        }

        basic_readline(linebuf, LINE_MAX);

        if (is_sv) {
            kstrncpy(g_str[idx], linebuf, STR_MAX);
            g_str[idx][STR_MAX] = '\0';
        } else if (is_arr) {
            g_arr[idx][(int)ai - 1] = str_to_int(linebuf);
        } else {
            g_num[idx] = str_to_int(linebuf);
        }

        skip_ws();
        if (*g_p == ',') { g_p++; bputs("? "); }
        else break;
    }
}

/* IF expr THEN stmt_or_lineno [ELSE stmt_or_lineno] */
static void exec_stmt(void);  /* forward */

static void exec_if(void) {
    /* Check if next is string comparison */
    int32_t cond;
    if (is_str_ahead()) {
        cond = eval_str_cmp();
    } else {
        cond = eval_num();
    }
    if (g_had_error) return;

    skip_ws();
    /* Accept THEN or GOTO */
    int is_goto = 0;
    if (match_kw("THEN")) { skip_ws(); }
    else if (match_kw("GOTO")) { is_goto = 1; skip_ws(); }
    else { basic_error("Expected THEN"); return; }

    if (cond) {
        /* Execute THEN clause */
        if (bisd(*g_p)) {
            int ln = parse_lineno(); do_goto(ln);
        } else if (!is_goto) {
            exec_stmt();
        }
        /* Skip ELSE if present (scan rest of line) */
    } else {
        /* Skip to ELSE if present */
        const char *p = g_p;
        int in_q = 0;
        while (*p) {
            if (*p == '"') { in_q = !in_q; p++; continue; }
            if (!in_q &&
                bu(p[0])=='E' && bu(p[1])=='L' && bu(p[2])=='S' && bu(p[3])=='E' &&
                !bisan(p[4])) {
                g_p = p + 4;
                skip_ws();
                if (bisd(*g_p)) { int ln = parse_lineno(); do_goto(ln); }
                else exec_stmt();
                return;
            }
            p++;
        }
        g_p = p; /* no ELSE, skip rest of line */
    }
}

/* FOR var = start TO limit [STEP step] */
static void exec_for(void) {
    skip_ws();
    if (!bisa(*g_p)) { basic_error("Syntax"); return; }
    int idx = bu(*g_p) - 'A'; g_p++;
    expect_char('=');
    int32_t start = eval_num();
    skip_ws(); if (!match_kw("TO")) { basic_error("Expected TO"); return; }
    int32_t limit = eval_num();
    int32_t step = 1;
    skip_ws(); if (match_kw("STEP")) step = eval_num();
    if (g_had_error) return;

    g_num[idx] = start;

    /* Check if loop should run at all */
    if ((step > 0 && start > limit) || (step < 0 && start < limit)) {
        /* Skip to matching NEXT */
        int depth = 1;
        g_pc++;
        while (g_pc < g_prog_size && depth > 0) {
            const char *p = g_prog[g_pc].text;
            while (*p == ' ') p++;
            if (bu(p[0])=='F'&&bu(p[1])=='O'&&bu(p[2])=='R'&&!bisan(p[3])) depth++;
            else if (bu(p[0])=='N'&&bu(p[1])=='E'&&bu(p[2])=='X'&&bu(p[3])=='T'&&!bisan(p[4])) depth--;
            if (depth > 0) g_pc++;
        }
        g_jumped = 1;
        return;
    }

    if (g_for_sp >= FOR_MAX) { basic_error("FOR overflow"); return; }
    g_for[g_for_sp].varidx  = idx;
    g_for[g_for_sp].limit   = limit;
    g_for[g_for_sp].step    = step;
    g_for[g_for_sp].body_pc = g_pc + 1;
    g_for_sp++;
}

/* NEXT [var] */
static void exec_next(void) {
    skip_ws();
    int idx = -1;
    if (bisa(*g_p)) { idx = bu(*g_p) - 'A'; g_p++; }

    /* Find matching FOR frame */
    int fi = g_for_sp - 1;
    if (idx >= 0) {
        for (fi = g_for_sp - 1; fi >= 0; fi--)
            if (g_for[fi].varidx == idx) break;
        if (fi < 0) { basic_error("NEXT without FOR"); return; }
    }
    if (fi < 0) { basic_error("NEXT without FOR"); return; }

    for_frame_t *fr = &g_for[fi];
    g_num[fr->varidx] += fr->step;
    int32_t v = g_num[fr->varidx];
    int cont = (fr->step > 0) ? (v <= fr->limit) : (v >= fr->limit);
    if (cont) {
        g_pc = fr->body_pc;
        g_jumped = 1;
    } else {
        /* Pop frame (and any inner frames) */
        g_for_sp = fi;
    }
}

/* GOSUB lineno */
static void exec_gosub(void) {
    skip_ws();
    int ln = parse_lineno();
    if (g_gosub_sp >= GOSUB_MAX) { basic_error("GOSUB overflow"); return; }
    g_gosub[g_gosub_sp++] = g_pc + 1;
    do_goto(ln);
}

/* RETURN */
static void exec_return(void) {
    if (g_gosub_sp <= 0) { basic_error("RETURN without GOSUB"); return; }
    g_pc = g_gosub[--g_gosub_sp];
    g_jumped = 1;
}

/* READ var [, var ...] */
static void exec_read(void) {
    for (;;) {
        skip_ws();
        if (!bisa(*g_p)) break;
        int idx = bu(*g_p) - 'A'; g_p++;
        int is_sv = (*g_p == '$'); if (is_sv) g_p++;

        int is_arr = 0; int32_t ai = 0;
        if (!is_sv && *g_p == '(') {
            is_arr = 1; g_p++;
            ai = eval_num(); expect_char(')');
            if (g_had_error) return;
            if (ai < 1 || ai > g_arr_dim[idx]) { basic_error("Array bounds"); return; }
        }

        if (g_data_ptr >= g_data_cnt) { basic_error("Out of DATA"); return; }
        const char *datum = g_data[g_data_ptr++];
        if (is_sv) {
            kstrncpy(g_str[idx], datum, STR_MAX);
            g_str[idx][STR_MAX] = '\0';
        } else if (is_arr) {
            g_arr[idx][(int)ai - 1] = str_to_int(datum);
        } else {
            g_num[idx] = str_to_int(datum);
        }

        skip_ws(); if (*g_p == ',') g_p++; else break;
    }
}

/* DIM var(n) [, var(n) ...] */
static void exec_dim(void) {
    for (;;) {
        skip_ws();
        if (!bisa(*g_p)) break;
        int idx = bu(*g_p) - 'A'; g_p++;
        skip_ws(); expect_char('(');
        int32_t n = eval_num(); expect_char(')');
        if (g_had_error) return;
        if (n < 1 || n > ARRAY_MAX) { basic_error("Invalid DIM"); return; }
        g_arr_dim[idx] = (int)n;
        kmemset(g_arr[idx], 0, sizeof(g_arr[idx]));
        skip_ws(); if (*g_p == ',') g_p++; else break;
    }
}

/* POKE addr, val */
static void exec_poke(void) {
    int32_t addr = eval_num();
    expect_char(',');
    int32_t val  = eval_num();
    if (!g_had_error)
        *(volatile uint8_t *)(uintptr_t)(uint32_t)addr = (uint8_t)(val & 0xFF);
}

/* ON expr GOTO/GOSUB n1, n2, ... */
static void exec_on(void) {
    int32_t sel = eval_num(); skip_ws();
    int is_sub = 0;
    if (match_kw("GOSUB")) is_sub = 1;
    else if (!match_kw("GOTO")) { basic_error("Expected GOTO/GOSUB"); return; }
    /* Collect line numbers */
    int lines[32]; int cnt = 0;
    for (;;) {
        skip_ws(); if (!bisd(*g_p)) break;
        if (cnt < 32) lines[cnt++] = parse_lineno();
        skip_ws(); if (*g_p == ',') g_p++; else break;
    }
    if (sel < 1 || sel > cnt) return; /* out of range: no action */
    if (is_sub) {
        if (g_gosub_sp >= GOSUB_MAX) { basic_error("GOSUB overflow"); return; }
        g_gosub[g_gosub_sp++] = g_pc + 1;
    }
    do_goto(lines[(int)sel - 1]);
}

/* SWAP var, var */
static void exec_swap(void) {
    skip_ws();
    if (!bisa(*g_p)) { basic_error("Syntax"); return; }
    int idx1 = bu(*g_p) - 'A'; g_p++;
    int sv1 = (*g_p == '$'); if (sv1) g_p++;
    expect_char(',');
    skip_ws();
    if (!bisa(*g_p)) { basic_error("Syntax"); return; }
    int idx2 = bu(*g_p) - 'A'; g_p++;
    int sv2 = (*g_p == '$'); if (sv2) g_p++;
    if (sv1 != sv2) { basic_error("Type mismatch"); return; }
    if (sv1) {
        char tmp[STR_MAX+1];
        kstrncpy(tmp, g_str[idx1], STR_MAX);
        kstrncpy(g_str[idx1], g_str[idx2], STR_MAX);
        kstrncpy(g_str[idx2], tmp, STR_MAX);
    } else {
        int32_t tmp = g_num[idx1];
        g_num[idx1] = g_num[idx2];
        g_num[idx2] = tmp;
    }
}

/* ================================================================
 * Statement dispatcher
 * ================================================================ */
static void exec_stmt(void) {
    skip_ws();
    if (!*g_p || *g_p == ':') return;

    /* Comments */
    if (*g_p == '\'') return;
    if (match_kw("REM")) { while (*g_p) g_p++; return; }

    /* Core statements */
    if (match_kw("PRINT") || *g_p == '?') {
        if (*g_p == '?') g_p++;
        exec_print(); return;
    }
    if (match_kw("INPUT"))   { exec_input();  return; }
    if (match_kw("LET"))     { exec_let();    return; }
    if (match_kw("IF"))      { exec_if();     return; }
    if (match_kw("GOTO"))    { int ln=parse_lineno(); do_goto(ln); return; }
    if (match_kw("GOSUB"))   { exec_gosub();  return; }
    if (match_kw("RETURN"))  { exec_return(); return; }
    if (match_kw("FOR"))     { exec_for();    return; }
    if (match_kw("NEXT"))    { exec_next();   return; }
    if (match_kw("DATA"))    { while (*g_p) g_p++; return; } /* handled by collect_data */
    if (match_kw("READ"))    { exec_read();   return; }
    if (match_kw("RESTORE")) { g_data_ptr = 0; return; }
    if (match_kw("DIM"))     { exec_dim();    return; }
    if (match_kw("ON"))      { exec_on();     return; }
    if (match_kw("SWAP"))    { exec_swap();   return; }
    if (match_kw("POKE"))    { exec_poke();   return; }
    if (match_kw("CLS"))     { vga_clear(); vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK); g_col=0; return; }
    if (match_kw("BEEP"))    { return; /* no speaker yet */ }
    if (match_kw("END") || match_kw("STOP")) { g_running = 0; return; }

    /* Implicit LET: variable = expr */
    if (bisa(*g_p)) { exec_let(); return; }

    basic_error("Unknown statement");
}

/* ================================================================
 * Execute one program line
 * ================================================================ */
static void exec_line(int idx) {
    g_p = g_prog[idx].text;
    while (*g_p && !g_had_error && g_running) {
        skip_ws();
        if (!*g_p || *g_p == '\'') break;
        exec_stmt();
        if (g_jumped) break;
        skip_ws();
        if (*g_p == ':') { g_p++; continue; }
        break;
    }
}

/* ================================================================
 * Run the program
 * ================================================================ */
static void run_program(int start_pc) {
    collect_data();
    g_pc       = start_pc;
    g_running  = 1;
    g_had_error = 0;
    g_gosub_sp = 0;
    g_for_sp   = 0;
    g_col      = 0;

    while (g_running && !g_had_error && g_pc < g_prog_size) {
        g_jumped = 0;
        exec_line(g_pc);
        if (!g_jumped) g_pc++;
    }

    if (!g_had_error && !g_running) {
        vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
        bputs("\nOk\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    }
}

/* ================================================================
 * REPL commands
 * ================================================================ */
static void do_list(const char *args) {
    /* LIST [from[-to]] */
    int from = 0, to = 99999;
    const char *p = args;
    while (*p == ' ') p++;
    if (bisd(*p)) {
        from = 0; while (bisd(*p)) from = from*10 + (*p++ - '0');
        if (*p == '-') {
            p++; to = 0; while (bisd(*p)) to = to*10 + (*p++ - '0');
        } else to = from;
    }

    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    for (int i = 0; i < g_prog_size; i++) {
        int ln = g_prog[i].linenum;
        if (ln < from || ln > to) continue;
        vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        bprint_uint((uint32_t)ln);
        bputc(' ');
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        bputs(g_prog[i].text);
        bputc('\n');
    }
    if (g_prog_size == 0) {
        vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
        bputs("(empty program)\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    }
}

static void do_run(const char *args) {
    int start = 0;
    const char *p = args;
    while (*p == ' ') p++;
    if (bisd(*p)) {
        int ln = 0; while (bisd(*p)) ln = ln*10 + (*p++ - '0');
        start = find_line(ln);
        if (start < 0) { bputs("?Undefined line\n"); return; }
    }
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    run_program(start);
}

static void do_save(void) {
    kmemcpy(g_saved, g_prog, (size_t)g_prog_size * sizeof(prog_line_t));
    g_saved_size = g_prog_size;
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    bputs("Program saved to memory bank.\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

static void do_load(void) {
    kmemcpy(g_prog, g_saved, (size_t)g_saved_size * sizeof(prog_line_t));
    g_prog_size = g_saved_size;
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    bputs("Program loaded from memory bank.\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

static void do_help(void) {
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    bputs("\n  InteiliBASIC v1.0 Quick Reference\n");
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    bputs("  -----------------------------------------------\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    bputs(
    "  Entering programs:\n"
    "    <lineno> <statement>    Store a line (e.g.  10 PRINT \"Hello\")\n"
    "    <lineno>                Delete that line\n"
    "\n"
    "  REPL commands (no line number):\n"
    "    RUN [lineno]  LIST [from[-to]]  NEW  SAVE  LOAD  HELP  BYE\n"
    "\n"
    "  Statements:\n"
    "    PRINT expr[;expr][,expr]   INPUT [\"prompt\";] var\n"
    "    LET var = expr             IF expr THEN stmt [ELSE stmt]\n"
    "    GOTO lineno                GOSUB lineno  /  RETURN\n"
    "    FOR v=s TO e [STEP n]      NEXT [v]\n"
    "    DIM v(n)                   DATA n,n,...  /  READ v  /  RESTORE\n"
    "    ON expr GOTO n,n,...       SWAP v1,v2\n"
    "    POKE addr,val              CLS   END   STOP   REM\n"
    "\n"
    "  Operators:  + - * / MOD   = <> < > <= >=   AND OR NOT\n"
    "\n"
    "  Numeric functions:\n"
    "    ABS(n)  SGN(n)  INT(n)  RND(n)  LEN(s$)\n"
    "    VAL(s$)  ASC(s$)  POS(0)  PEEK(addr)\n"
    "\n"
    "  String functions:\n"
    "    CHR$(n)  STR$(n)  LEFT$(s$,n)  RIGHT$(s$,n)\n"
    "    MID$(s$,pos[,len])  INKEY$  SPACE$(n)  STRING$(n,c$)\n"
    "\n"
    "  Variables: A-Z (integer)   A$-Z$ (string)   A(n)-Z(n) (array)\n"
    "  Numbers are 32-bit signed integers. Strings up to 79 chars.\n"
    "  RND(n) returns random integer 1..n\n"
    );
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    bputs("  -----------------------------------------------\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

/* ================================================================
 * Sample demo programs
 * ================================================================ */
static void load_demo(int which) {
    do_new();
    if (which == 1) {
        /* Fibonacci */
        insert_line(10,  "REM Fibonacci sequence");
        insert_line(20,  "LET A = 0");
        insert_line(30,  "LET B = 1");
        insert_line(40,  "FOR I = 1 TO 15");
        insert_line(50,  "PRINT A;");
        insert_line(60,  "LET C = A + B");
        insert_line(70,  "LET A = B");
        insert_line(80,  "LET B = C");
        insert_line(90,  "NEXT I");
        insert_line(100, "PRINT");
        insert_line(110, "END");
    } else if (which == 2) {
        /* Guess the number */
        insert_line(10,  "REM Guess the number");
        insert_line(20,  "LET N = RND(100)");
        insert_line(30,  "LET T = 0");
        insert_line(40,  "PRINT \"I'm thinking of a number 1-100.\"");
        insert_line(50,  "INPUT \"Your guess: \"; G");
        insert_line(60,  "LET T = T + 1");
        insert_line(70,  "IF G = N THEN GOTO 120");
        insert_line(80,  "IF G < N THEN PRINT \"Too low!\"");
        insert_line(90,  "IF G > N THEN PRINT \"Too high!\"");
        insert_line(100, "GOTO 50");
        insert_line(120, "PRINT \"Correct! Took\"; T; \"guesses.\"");
        insert_line(130, "END");
    } else if (which == 3) {
        /* Times table */
        insert_line(10,  "REM Times table");
        insert_line(20,  "FOR I = 1 TO 10");
        insert_line(30,  "FOR J = 1 TO 10");
        insert_line(40,  "PRINT I*J,");
        insert_line(50,  "NEXT J");
        insert_line(60,  "PRINT");
        insert_line(70,  "NEXT I");
        insert_line(80,  "END");
    } else if (which == 4) {
        /* String manipulation */
        insert_line(10,  "REM String demo");
        insert_line(20,  "INPUT \"Your name: \"; N$");
        insert_line(30,  "PRINT \"Hello, \"; N$; \"!\"");
        insert_line(40,  "PRINT \"Length:\"; LEN(N$)");
        insert_line(50,  "PRINT \"Upper 3: \"; LEFT$(N$, 3)");
        insert_line(60,  "PRINT \"ASCII of first char:\"; ASC(N$)");
        insert_line(70,  "END");
    }
}

/* ================================================================
 * Main entry point
 * ================================================================ */
void basic_run(void) {
    /* Reset program but keep variables between sessions? No -- full reset. */
    do_new();
    g_saved_size = 0;
    g_rnd = 54321;
    g_col = 0;

    /* Banner */
    vga_clear();
    vga_set_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_CYAN);
    bputs("  InteiliBASIC v1.0 -- Integer BASIC for inteilidOS  ");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    bputs("\n\n");
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    bputs("  32-bit integer BASIC.  Type HELP for commands.  BYE to exit.\n");
    vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    bputs("  DEMO 1=Fibonacci  2=Guess  3=Times table  4=Strings\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    bputs("\nOk\n");

    char linebuf[LINE_MAX];

    for (;;) {
        /* Prompt */
        vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        bputs("> ");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        g_col = 2;

        basic_readline(linebuf, LINE_MAX);
        g_col = 0;

        /* Trim leading spaces */
        char *p = linebuf;
        while (*p == ' ') p++;
        if (!*p) continue;

        /* Numbered line: store it */
        if (bisd(*p)) {
            int ln = 0;
            const char *q = p;
            while (bisd(*q)) ln = ln*10 + (*q++ - '0');
            while (*q == ' ') q++;
            if (*q) insert_line(ln, q);
            else    delete_line(ln);
            continue;
        }

        /* Direct command (uppercase for matching) */
        /* Make a lowercase-safe comparison on first word */
        char cmd[16]; int ci = 0;
        const char *q = p;
        while (bisa(*q) && ci < 15) cmd[ci++] = (char)bu(*q++);
        cmd[ci] = '\0';
        const char *args = q; while (*args == ' ') args++;

        if (kstrcmp(cmd, "BYE")  == 0 || kstrcmp(cmd, "QUIT") == 0) break;
        if (kstrcmp(cmd, "NEW")  == 0) { do_new(); bputs("Ok\n"); continue; }
        if (kstrcmp(cmd, "RUN")  == 0) { do_run(args); continue; }
        if (kstrcmp(cmd, "LIST") == 0) { do_list(args); continue; }
        if (kstrcmp(cmd, "SAVE") == 0) { do_save(); continue; }
        if (kstrcmp(cmd, "LOAD") == 0) { do_load(); continue; }
        if (kstrcmp(cmd, "HELP") == 0) { do_help(); continue; }
        if (kstrcmp(cmd, "CLS")  == 0) {
            vga_clear(); vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            g_col = 0; continue;
        }
        if (kstrcmp(cmd, "DEMO") == 0) {
            int d = args[0] - '0';
            if (d < 1 || d > 4) { bputs("?DEMO 1-4\n"); continue; }
            load_demo(d);
            bputs("Demo loaded. Type LIST to view, RUN to run.\n");
            continue;
        }

        /* Direct execution (no line number) */
        g_had_error = 0;
        g_running   = 1;
        g_jumped    = 0;
        g_pc        = -1;   /* not in a program line */
        g_col       = 0;
        g_p         = p;

        while (*g_p && !g_had_error && g_running) {
            exec_stmt();
            skip_ws();
            if (*g_p == ':') { g_p++; continue; }
            break;
        }
        if (!g_had_error) bputs("\n");
    }

    /* Exit */
    vga_clear();
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    bputs("Returning to inteiliDOS shell...\n");
}
