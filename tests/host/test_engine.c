/* OpenEdit host tests: the engine (buffer, document, undo, find, patterns,
 * colour kinds). MIT, Copyright (c) 2026 Dalsin Limited. */
#include "oe_buf.h"
#include "oe_doc.h"
#include "oe_find.h"
#include "oe_pat.h"
#include "oe_syntax.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;

#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static unsigned long rnd_state = 12345;
static unsigned long rnd(void)
{
    rnd_state = rnd_state * 1103515245UL + 12345UL;
    return (rnd_state >> 8) & 0xffffff;
}

static char *whole(oe_doc *d)
{
    long n;
    return oe_doc_text(d, 0, oe_doc_len(d), &n);
}

static int lines_ok(oe_buf *b, const char *ref, long n)
{
    long i, line = 0;
    if (oe_buf_line_start(b, 0) != 0)
        return 0;
    for (i = 0; i < n; i++)
        if (ref[i] == '\n') {
            line++;
            if (oe_buf_line_start(b, line) != i + 1)
                return 0;
        }
    return oe_buf_lines(b) == line + 1;
}

static void test_buf(void)
{
    oe_buf b;
    char *ref = malloc(200000), *got = malloc(200000);
    long n = 0, k;
    CHECK(oe_buf_init(&b));
    for (k = 0; k < 20000; k++) {
        long pos = n ? (long)(rnd() % (n + 1)) : 0;
        if (rnd() % 3 || n < 10) {
            char s[8];
            int m = 1 + rnd() % 7, i;
            for (i = 0; i < m; i++)
                s[i] = "ab\ncd \n\t"[rnd() % 8];
            if (n + m >= 199000)
                continue;
            CHECK(oe_buf_insert(&b, pos, s, m));
            memmove(ref + pos + m, ref + pos, n - pos);
            memcpy(ref + pos, s, m);
            n += m;
        } else {
            long m = 1 + rnd() % 9;
            if (pos + m > n)
                m = n - pos;
            oe_buf_delete(&b, pos, m);
            memmove(ref + pos, ref + pos + m, n - pos - m);
            n -= m;
        }
        if (k % 997 == 0) {
            CHECK(oe_buf_len(&b) == n);
            CHECK(oe_buf_copy(&b, 0, n, got) == n && memcmp(got, ref, n) == 0);
            CHECK(lines_ok(&b, ref, n));
        }
    }
    CHECK(oe_buf_len(&b) == n);
    CHECK(oe_buf_copy(&b, 0, n, got) == n && memcmp(got, ref, n) == 0);
    CHECK(lines_ok(&b, ref, n));
    /* line_of agrees with a walk */
    {
        long i, line = 0, bad = 0;
        for (i = 0; i <= n; i++) {
            if (oe_buf_line_of(&b, i) != line)
                bad++;
            if (i < n && ref[i] == '\n')
                line++;
        }
        CHECK(bad == 0);
    }
    oe_buf_free(&b);
    free(ref);
    free(got);
}

static void load(oe_doc *d, const char *s)
{
    CHECK(oe_doc_load(d, s, (long)strlen(s)));
}

static int is(oe_doc *d, const char *want)
{
    char *t = whole(d);
    int ok = t && strcmp(t, want) == 0;
    if (!ok)
        printf("  have \"%s\"\n  want \"%s\"\n", t ? t : "(null)", want);
    free(t);
    return ok;
}

static char *saved(oe_doc *d, long *n)
{
    char *o;
    *n = oe_doc_save_size(d);
    o = malloc(*n + 1);
    oe_doc_save_bytes(d, o);
    o[*n] = 0;
    return o;
}

static void test_doc_io(void)
{
    oe_doc d;
    long n;
    char *o;
    oe_doc_init(&d);
    load(&d, "one\r\ntwo\r\nthree");
    CHECK(d.eol == OE_EOL_CRLF);
    CHECK(is(&d, "one\ntwo\nthree"));
    o = saved(&d, &n);
    CHECK(n == 15 && memcmp(o, "one\r\ntwo\r\nthree", 15) == 0);
    free(o);
    load(&d, "mac\rfile\r");
    CHECK(d.eol == OE_EOL_CR && is(&d, "mac\nfile\n"));
    o = saved(&d, &n);
    CHECK(strcmp(o, "mac\rfile\r") == 0);
    free(o);
    load(&d, "caf\xc3\xa9 \xe2\x82\xac\n");
    CHECK(d.cs == OE_CS_UTF8);
    load(&d, "caf\xe9\n");
    CHECK(d.cs == OE_CS_LATIN1);
    CHECK(oe_doc_load(&d, "\xef\xbb\xbfhi", 5));
    CHECK(d.bom && d.cs == OE_CS_UTF8 && is(&d, "hi"));
    o = saved(&d, &n);
    CHECK(n == 5 && memcmp(o, "\xef\xbb\xbfhi", 5) == 0);
    free(o);
    /* UTF-8: the caret steps over whole characters, a column each. */
    load(&d, "a\xc3\xa9\xe2\x82\xac" "b");
    oe_doc_set_caret(&d, 0, 0);
    oe_doc_move(&d, OE_RIGHT, 0, 0);
    oe_doc_move(&d, OE_RIGHT, 0, 0);
    CHECK(d.caret == 3);
    oe_doc_move(&d, OE_RIGHT, 0, 0);
    CHECK(d.caret == 6 && oe_doc_col(&d, 6) == 3);
    oe_doc_backspace(&d);
    CHECK(is(&d, "a\xc3\xa9" "b"));
    {
        char out[16];
        int src[16], k = oe_doc_show(&d, 0, 0, 16, out, src);
        CHECK(k == 3 && out[1] == (char)0xe9 && src[2] == 3);
    }
    oe_doc_free(&d);
}

static void type(oe_doc *d, const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            oe_doc_newline(d);
        else
            oe_doc_insert(d, s, 1, 1);
    }
}

static void test_doc_edit(void)
{
    oe_doc d;
    oe_doc_init(&d);
    load(&d, "");
    type(&d, "hello world");
    CHECK(is(&d, "hello world") && oe_doc_modified(&d));
    oe_doc_undo(&d);
    CHECK(is(&d, "hello"));
    oe_doc_undo(&d);
    CHECK(is(&d, "") && !oe_doc_modified(&d));
    oe_doc_redo(&d);
    oe_doc_redo(&d);
    CHECK(is(&d, "hello world") && d.caret == 11);
    oe_doc_mark_saved(&d);
    CHECK(!oe_doc_modified(&d));
    type(&d, "!");
    CHECK(oe_doc_modified(&d));
    oe_doc_undo(&d);
    CHECK(!oe_doc_modified(&d) && is(&d, "hello world"));

    /* Auto-indent and the home key. */
    load(&d, "    IF EXISTS x");
    oe_doc_move(&d, OE_END, 0, 0);
    type(&d, "\nEcho");
    CHECK(is(&d, "    IF EXISTS x\n    Echo"));
    oe_doc_move(&d, OE_HOME, 0, 0);
    CHECK(d.caret == 20);
    oe_doc_move(&d, OE_HOME, 0, 0);
    CHECK(d.caret == 16);

    /* Backspace merges into one undo; a selection is replaced. */
    load(&d, "abcdef");
    oe_doc_set_caret(&d, 6, 0);
    oe_doc_backspace(&d);
    oe_doc_backspace(&d);
    oe_doc_backspace(&d);
    CHECK(is(&d, "abc"));
    oe_doc_undo(&d);
    CHECK(is(&d, "abcdef") && d.caret == 6);
    oe_doc_select(&d, 1, 4);
    type(&d, "X");
    CHECK(is(&d, "aXef"));
    oe_doc_undo(&d);
    CHECK(is(&d, "abcdef") && d.anchor == 1 && d.caret == 4);

    /* Block indent and outdent, one undo each. */
    load(&d, "a\nb\nc\n");
    oe_doc_select(&d, 0, 4);
    oe_doc_tab(&d, 0);
    CHECK(is(&d, "\ta\n\tb\nc\n"));
    oe_doc_tab(&d, 1);
    CHECK(is(&d, "a\nb\nc\n"));
    oe_doc_undo(&d);
    oe_doc_undo(&d);
    CHECK(is(&d, "a\nb\nc\n"));

    /* Up and down keep the column; tabs count as their width. */
    load(&d, "\tx\nabcdefghij\nab");
    oe_doc_set_caret(&d, 2, 0);
    CHECK(oe_doc_col(&d, 2) == 9);
    oe_doc_move(&d, OE_DOWN, 0, 0);
    CHECK(d.caret == 3 + 9);
    oe_doc_move(&d, OE_DOWN, 0, 0);
    CHECK(d.caret == 16);
    oe_doc_move(&d, OE_UP, 0, 0);
    CHECK(d.caret == 12);

    /* Words. */
    load(&d, "Assign Games: DH1:Games");
    oe_doc_set_caret(&d, 0, 0);
    oe_doc_move(&d, OE_WORDR, 0, 0);
    CHECK(d.caret == 7);
    oe_doc_move(&d, OE_WORDR, 1, 0);
    CHECK(d.caret == 14 && d.anchor == 7);
    oe_doc_select_word(&d, 9);
    CHECK(d.anchor == 7 && d.caret == 12);

    /* Read-only refuses edits. */
    d.readonly = 1;
    CHECK(!oe_doc_insert(&d, "x", 1, 1));
    d.readonly = 0;

    /* A random edit storm undoes to the start and redoes to the end. */
    {
        int k;
        char *end;
        load(&d, "The quick brown fox\njumps over\nthe lazy dog\n");
        for (k = 0; k < 3000; k++) {
            long len = oe_doc_len(&d);
            int what = rnd() % 6;
            oe_doc_set_caret(&d, len ? (long)(rnd() % (len + 1)) : 0, 0);
            if (what == 0)
                oe_doc_select(&d, d.caret, len ? (long)(rnd() % (len + 1)) : 0);
            if (what <= 2)
                oe_doc_insert(&d, "xy\nz" + rnd() % 4, 1, rnd() % 2);
            else if (what == 3)
                oe_doc_backspace(&d);
            else if (what == 4)
                oe_doc_delete(&d);
            else
                oe_doc_newline(&d);
        }
        end = whole(&d);
        while (oe_doc_undo(&d))
            ;
        CHECK(is(&d, "The quick brown fox\njumps over\nthe lazy dog\n"));
        CHECK(!oe_doc_modified(&d));
        while (oe_doc_redo(&d))
            ;
        CHECK(is(&d, end));
        free(end);
    }
    oe_doc_free(&d);
}

static void test_pat(void)
{
    const char *s = "Assign Games: DH1:Games";
    const char *e;
    CHECK(oe_pat_whole("#?.info", "Disk.info", 1));
    CHECK(!oe_pat_whole("#?.info", "Disk.inf", 1));
    CHECK(oe_pat_whole("(S:|SYS:S/)#?", "s:User-Startup", 1));
    CHECK(oe_pat_whole("#?.(c|h)", "main.C", 1));
    CHECK(!oe_pat_whole("#?.(c|h)", "main.C", 0));
    CHECK(oe_pat_whole("[a-c]#[0-9]x", "b123x", 0));
    CHECK(oe_pat_whole("[~a-c]x", "dx", 0) && !oe_pat_whole("[~a-c]x", "bx", 0));
    CHECK(oe_pat_whole("ab(s|%)", "ab", 0) && oe_pat_whole("ab(s|%)", "abs", 0));
    CHECK(oe_pat_whole("'#'?", "#?", 0) && !oe_pat_whole("'#'?", "#x", 0));
    CHECK(oe_pat_whole("*.guide", "OpenEdit.guide", 1));
    CHECK(oe_pat_whole("#(ab)c", "ababc", 0) && oe_pat_whole("#(ab)c", "c", 0));
    e = oe_pat_at("G#?s", 4, s + 7, s + strlen(s), 1);
    CHECK(e == s + strlen(s));          /* longest */
    e = oe_pat_at("DH?:", 4, s + 14, s + strlen(s), 0);
    CHECK(e == s + 18);
}

static void test_find(void)
{
    oe_doc d;
    long a, b, idx;
    oe_doc_init(&d);
    load(&d, "Assign Games: DH1:Games\nassign Music: DH1:Music\nReassign\n");
    CHECK(oe_find(&d, "assign", 0, 0, 0, &a, &b) && a == 0 && b == 6);
    CHECK(oe_find(&d, "assign", 0, 1, 0, &a, &b) && a == 24);
    CHECK(oe_find(&d, "assign", OE_F_CASE, 0, 0, &a, &b) && a == 24);
    CHECK(oe_find(&d, "assign", OE_F_WORD, 25, 0, &a, &b) && a == 0);   /* wraps, skips Reassign */
    CHECK(oe_find(&d, "assign", 0, 25, 0, &a, &b) && a == 50);
    CHECK(oe_find(&d, "assign", 0, 24, 1, &a, &b) && a == 0);
    CHECK(oe_find(&d, "assign", 0, 0, 1, &a, &b) && a == 50);           /* wraps backward */
    CHECK(oe_find(&d, "DH?:#?", OE_F_PAT, 0, 0, &a, &b) && a == 14 && b == 23);
    CHECK(oe_find_count(&d, "assign", 0, 24, &idx) == 3 && idx == 2);
    CHECK(!oe_find(&d, "nothing", 0, 0, 0, &a, &b));
    CHECK(oe_replace_all(&d, "DH1:", 0, "Work:") == 2);
    CHECK(is(&d, "Assign Games: Work:Games\nassign Music: Work:Music\nReassign\n"));
    CHECK(oe_replace_all(&d, "a", OE_F_CASE, "aa") == 4);
    CHECK(is(&d, "Assign Gaames: Work:Gaames\naassign Music: Work:Music\nReaassign\n"));
    oe_doc_undo(&d);
    CHECK(is(&d, "Assign Games: Work:Games\nassign Music: Work:Music\nReassign\n"));
    oe_doc_free(&d);
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *t;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    t = malloc(n + 1);
    n = (long)fread(t, 1, n, f);
    t[n] = 0;
    fclose(f);
    return t;
}

static const char *kind_files[] = { "AmigaDOS", "ARexx", "C", "Assembler", "AmigaGuide", "HTML", "Lua", "Makefile" };
#define NK 8

static void classes(const oe_kind *k, const char *line, char *out)
{
    unsigned char cls[256];
    int n = (int)strlen(line), i;
    oe_syn_line(k, line, n, 0, cls);
    for (i = 0; i < n; i++)
        out[i] = "TCKSVNLc"[cls[i]];
    out[n] = 0;
}

static void test_syntax(const char *kdir)
{
    oe_kind kinds[NK];
    char path[512], err[96], got[256];
    int i, st;
    unsigned char cls[256];
    for (i = 0; i < NK; i++) {
        char *t;
        snprintf(path, sizeof path, "%s/%s.kind", kdir, kind_files[i]);
        t = read_file(path);
        CHECK(t != NULL);
        if (!t)
            return;
        if (!oe_kind_parse(&kinds[i], t, err, sizeof err))
            printf("  %s: %s\n", path, err);
        CHECK(kinds[i].name[0]);
        free(t);
    }
    CHECK(oe_kind_pick(kinds, NK, "S:User-Startup", "", 0) == 0);
    CHECK(oe_kind_pick(kinds, NK, "Work:Mail.rexx", "", 0) == 1);
    CHECK(oe_kind_pick(kinds, NK, "RAM:x", "/* hello */", 11) == 1);
    CHECK(oe_kind_pick(kinds, NK, "Work:src/main.c", "", 0) == 2);
    CHECK(oe_kind_pick(kinds, NK, "Work:startup.s", "", 0) == 3);
    CHECK(oe_kind_pick(kinds, NK, "Help:OpenEdit.guide", "", 0) == 4);
    CHECK(oe_kind_pick(kinds, NK, "Work:Makefile", "", 0) == 7);
    CHECK(oe_kind_pick(kinds, NK, "Work:readme", "Hello", 5) == -1);

    classes(&kinds[0], "Assign Games: DH1:Games ; games", got);
    CHECK(strcmp(got, "CCCCCCTTTTTTTTTTTTTTTTTTccccccc") == 0);
    classes(&kinds[0], "IF $host EQ \"a*\"b\"", got);
    CHECK(strcmp(got, "KKTVVVVVTKKTSSSSSS") == 0);
    classes(&kinds[0], "SKIP nonet", got);
    CHECK(strcmp(got, "KKKKTLLLLL") == 0);
    classes(&kinds[0], ".key FILE/A", got);
    CHECK(strncmp(got, "KKKK", 4) == 0);

    /* ARexx: nested comments over lines, labels, strings. */
    st = oe_syn_line(&kinds[1], "/* a /* b */", 12, 0, cls);
    CHECK(st == 1);
    st = oe_syn_line(&kinds[1], "still */ SAY 'x'", 16, st, cls);
    CHECK(st == 0 && cls[0] == OE_C_COMMENT && cls[7] == OE_C_COMMENT && cls[9] == OE_C_KEYWORD && cls[13] == OE_C_STRING);
    classes(&kinds[1], "error: SAY x", got);
    CHECK(strcmp(got, "LLLLLLTKKKTT") == 0);

    classes(&kinds[2], "#include <x.h> // c", got);
    CHECK(strncmp(got, "KKKKKKKK", 8) == 0 && got[15] == 'c');
    classes(&kinds[2], "int n = 0x1f; \"a\\\"b\"", got);
    CHECK(strcmp(got, "CCCTTTTTNNNNTTSSSSSS") == 0);

    classes(&kinds[3], "loop:\tmove.l d0,(a0)+ ; x", got);
    CHECK(strncmp(got, "LLLLLTCCCCCC", 12) == 0 && got[24] == 'c');
    classes(&kinds[3], "* note", got);
    CHECK(strcmp(got, "cccccc") == 0);
    classes(&kinds[3], "\tmove.w #$dff,d0", got);
    CHECK(got[9] == 'N');

    classes(&kinds[4], "@node Main \"Title\"", got);
    CHECK(strncmp(got, "KKKKK", 5) == 0 && got[11] == 'S');
    classes(&kinds[4], "a @{b}bold", got);
    CHECK(strcmp(got, "TTCCCCTTTT") == 0);

    st = oe_syn_line(&kinds[5], "<a href=\"x\"", 11, 0, cls);
    CHECK(st == 0x100 && cls[0] == OE_C_COMMAND && cls[8] == OE_C_STRING);
    st = oe_syn_line(&kinds[5], ">x<!-- c", 8, st, cls);
    CHECK(cls[0] == OE_C_COMMAND && cls[1] == OE_C_TEXT && cls[2] == OE_C_COMMENT && st == 1);

    classes(&kinds[6], "local x = 1 -- c", got);
    CHECK(strcmp(got, "KKKKKTTTTTNTcccc") == 0);
    st = oe_syn_line(&kinds[6], "--[[ a", 6, 0, cls);
    CHECK(st == 1);

    classes(&kinds[7], "all: $(OBJ) # x", got);
    CHECK(strcmp(got, "LLLLTVVVVVVTccc") == 0);

    /* The document's state cache follows edits. */
    {
        oe_doc d;
        oe_doc_init(&d);
        load(&d, "/* a\nb\nc */\nSAY x\n");
        d.kind = 1;
        CHECK(oe_syn_state(&d, &kinds[1], 3) == 0);
        CHECK(oe_syn_state(&d, &kinds[1], 2) == 1);
        oe_doc_set_caret(&d, 0, 0);
        oe_doc_delete(&d);          /* "* a": no longer a comment */
        CHECK(oe_syn_state(&d, &kinds[1], 2) == 0);
        oe_doc_free(&d);
    }
    for (i = 0; i < NK; i++)
        oe_kind_free(&kinds[i]);
}

int main(int argc, char **argv)
{
    test_buf();
    test_doc_io();
    test_doc_edit();
    test_pat();
    test_find();
    test_syntax(argc > 1 ? argv[1] : "kinds");
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
