/* oe_syntax: colour kinds (oe_syntax.h).
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include "oe_syntax.h"
#include "oe_pat.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* State bits carried from line to line. */
#define ST_DEPTH 0xff       /* block comment depth */
#define ST_TAG   0x100      /* inside an HTML tag */

static int is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static int cmp_word(const oe_kind *k, const char *a, int an, const char *b)
{
    int i;
    for (i = 0; i < an && b[i]; i++) {
        int x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (k->nocase) {
            x = oe_lower(x);
            y = oe_lower(y);
        }
        if (x != y)
            return x - y;
    }
    if (i < an)
        return 1;
    return b[i] ? -1 : 0;
}

static const oe_kind *sort_kind;

static int sort_cmp(const void *pa, const void *pb)
{
    const char *a = sort_kind->pool + ((const struct oe_word *)pa)->off;
    const char *b = sort_kind->pool + ((const struct oe_word *)pb)->off;
    return cmp_word(sort_kind, a, (int)strlen(a), b);
}

static int add_word(oe_kind *k, const char *w, int n, int cls)
{
    if (k->pooln + n + 1 > k->poolcap) {
        long nc = (k->poolcap + n + 1) * 2 + 256;
        char *np = realloc(k->pool, nc);
        if (!np)
            return 0;
        k->pool = np;
        k->poolcap = nc;
    }
    if (k->nwords == k->wcap) {
        int nc = k->wcap ? k->wcap * 2 : 64;
        struct oe_word *nw = realloc(k->words, nc * sizeof *nw);
        if (!nw)
            return 0;
        k->words = nw;
        k->wcap = nc;
    }
    memcpy(k->pool + k->pooln, w, n);
    k->pool[k->pooln + n] = 0;
    k->words[k->nwords].off = k->pooln;
    k->words[k->nwords].cls = cls;
    k->nwords++;
    k->pooln += n + 1;
    return 1;
}

static void copy_val(char *dst, int size, const char *v, int vn)
{
    if (vn >= size)
        vn = size - 1;
    memcpy(dst, v, vn);
    dst[vn] = 0;
}

static int on(const char *v, int vn)
{
    return !(vn >= 3 && (strncmp(v, "off", 3) == 0 || strncmp(v, "OFF", 3) == 0)) &&
           !(vn >= 2 && (strncmp(v, "no", 2) == 0 || strncmp(v, "No", 2) == 0));
}

/* The next space-separated word of v..ve. */
static const char *next_tok(const char **v, const char *ve, int *n)
{
    const char *s;
    while (*v < ve && is_space(**v))
        (*v)++;
    s = *v;
    while (*v < ve && !is_space(**v))
        (*v)++;
    *n = (int)(*v - s);
    return *n ? s : NULL;
}

int oe_kind_parse(oe_kind *k, const char *text, char *err, int errlen)
{
    const char *p = text;
    int line = 0;
    memset(k, 0, sizeof *k);
    while (*p) {
        const char *e = p, *key, *v, *ve;
        int kn, n;
        while (*e && *e != '\n')
            e++;
        line++;
        ve = e;
        while (ve > p && is_space(ve[-1]))
            ve--;
        v = p;
        if (*p != '#' && (key = next_tok(&v, ve, &kn))) {
            while (v < ve && is_space(*v))
                v++;
            n = (int)(ve - v);
#define IS(s) (kn == (int)sizeof(s) - 1 && strncmp(key, s, kn) == 0)
            if (IS("name"))
                copy_val(k->name, sizeof k->name, v, n);
            else if (IS("files"))
                copy_val(k->files, sizeof k->files, v, n);
            else if (IS("paths"))
                copy_val(k->paths, sizeof k->paths, v, n);
            else if (IS("firstline"))
                copy_val(k->firstline, sizeof k->firstline, v, n);
            else if (IS("case"))
                k->nocase = !on(v, n);
            else if (IS("nest"))
                k->nest = on(v, n);
            else if (IS("numbers"))
                k->numbers = on(v, n);
            else if (IS("tags"))
                k->tags = on(v, n);
            else if (IS("comment")) {
                int j = k->comment[0][0] ? 1 : 0;
                copy_val(k->comment[j], sizeof k->comment[j], v, n);
            } else if (IS("comment0"))
                k->comment0 = n ? *v : 0;
            else if (IS("block") || IS("inline")) {
                const char *a, *b;
                int an, bn;
                const char *vv = v;
                a = next_tok(&vv, ve, &an);
                b = next_tok(&vv, ve, &bn);
                if (!a || !b) {
                    snprintf(err, errlen, "line %d: %.*s needs a start and an end", line, kn, key);
                    oe_kind_free(k);
                    return 0;
                }
                if (IS("block")) {
                    copy_val(k->block[0], sizeof k->block[0], a, an);
                    copy_val(k->block[1], sizeof k->block[1], b, bn);
                } else {
                    copy_val(k->inl[0], sizeof k->inl[0], a, an);
                    copy_val(k->inl[1], sizeof k->inl[1], b, bn);
                }
            } else if (IS("strings"))
                copy_val(k->strings, sizeof k->strings, v, n);
            else if (IS("escape"))
                k->escape = n ? *v : 0;
            else if (IS("variable"))
                k->variable = n ? *v : 0;
            else if (IS("directive"))
                k->directive = n ? *v : 0;
            else if (IS("suffix"))
                k->suffix = n ? *v : 0;
            else if (IS("labels")) {
                const char *vv = v, *w;
                int wn;
                w = next_tok(&vv, ve, &wn);
                if (w && wn == 5 && strncmp(w, "colon", 5) == 0)
                    k->label_colon = 1;
                else if (w && wn == 7 && strncmp(w, "column0", 7) == 0)
                    k->label_col0 = 1;
                else if (w && wn == 5 && strncmp(w, "after", 5) == 0)
                    while ((w = next_tok(&vv, ve, &wn)) && k->nafter < 4)
                        copy_val(k->after[k->nafter++], sizeof k->after[0], w, wn);
            } else if (IS("keywords") || IS("commands")) {
                const char *vv = v, *w;
                int wn, cls = IS("keywords") ? OE_C_KEYWORD : OE_C_COMMAND;
                while ((w = next_tok(&vv, ve, &wn)))
                    if (!add_word(k, w, wn, cls)) {
                        snprintf(err, errlen, "out of memory");
                        oe_kind_free(k);
                        return 0;
                    }
            }
#undef IS
        }
        p = *e ? e + 1 : e;
    }
    if (!k->name[0]) {
        snprintf(err, errlen, "no name");
        oe_kind_free(k);
        return 0;
    }
    sort_kind = k;
    if (k->nwords)
        qsort(k->words, k->nwords, sizeof *k->words, sort_cmp);
    return 1;
}

void oe_kind_free(oe_kind *k)
{
    free(k->pool);
    free(k->words);
    memset(k, 0, sizeof *k);
}

static int lookup(const oe_kind *k, const char *w, int n)
{
    int lo = 0, hi = k->nwords - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, c = cmp_word(k, w, n, k->pool + k->words[mid].off);
        if (!c)
            return k->words[mid].cls;
        if (c < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    return OE_C_TEXT;
}

static const char *file_part(const char *path)
{
    const char *f = path, *p;
    for (p = path; *p; p++)
        if (*p == '/' || *p == ':')
            f = p + 1;
    return f;
}

int oe_kind_pick(oe_kind *kinds, int n, const char *path, const char *first, int firstn)
{
    int i;
    char line[96];
    for (i = 0; i < n; i++)
        if (kinds[i].paths[0] && path[0] && oe_pat_whole(kinds[i].paths, path, 1))
            return i;
    for (i = 0; i < n; i++)
        if (kinds[i].files[0] && path[0] && oe_pat_whole(kinds[i].files, file_part(path), 1))
            return i;
    if (firstn > (int)sizeof line - 1)
        firstn = sizeof line - 1;
    memcpy(line, first, firstn);
    line[firstn] = 0;
    {
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = 0;
    }
    for (i = 0; i < n; i++)
        if (kinds[i].firstline[0] && oe_pat_whole(kinds[i].firstline, line, 1))
            return i;
    return -1;
}

static int starts(const char *s, int n, int i, const char *what)
{
    int k = (int)strlen(what);
    return k && i + k <= n && memcmp(s + i, what, k) == 0;
}

static void paint(unsigned char *cls, int a, int b, int c)
{
    for (; a < b; a++)
        cls[a] = (unsigned char)c;
}

static int is_digit(int c)
{
    return c >= '0' && c <= '9';
}

static int is_hex(int c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int oe_syn_line(const oe_kind *k, const char *s, int n, int state, unsigned char *cls)
{
    int i = 0, depth = state & ST_DEPTH, tag = state & ST_TAG, label_next = 0, lead = 1;
    memset(cls, OE_C_TEXT, n);
    if (!k)
        return 0;
    while (i < n) {
        int c = (unsigned char)s[i], j;
        if (depth) {
            /* Inside a block comment. */
            int st = i;
            while (i < n) {
                if (k->nest && starts(s, n, i, k->block[0])) {
                    depth++;
                    i += (int)strlen(k->block[0]);
                } else if (starts(s, n, i, k->block[1])) {
                    i += (int)strlen(k->block[1]);
                    if (!--depth)
                        break;
                } else
                    i++;
            }
            paint(cls, st, i, OE_C_COMMENT);
            continue;
        }
        if (tag) {
            if (c == '>') {
                cls[i++] = OE_C_COMMAND;
                tag = 0;
                continue;
            }
            if (c == '"' || c == '\'') {
                int st = i++;
                while (i < n && (unsigned char)s[i] != c)
                    i++;
                if (i < n)
                    i++;
                paint(cls, st, i, OE_C_STRING);
                continue;
            }
            i++;
            continue;
        }
        if (is_space(c)) {
            i++;
            continue;
        }
        if (i == 0 && k->comment0 && c == (unsigned char)k->comment0) {
            paint(cls, 0, n, OE_C_COMMENT);
            break;
        }
        if (k->block[0][0] && starts(s, n, i, k->block[0])) {
            int st = i;
            i += (int)strlen(k->block[0]);
            depth = 1;
            paint(cls, st, i, OE_C_COMMENT);
            lead = 0;
            continue;
        }
        for (j = 0; j < 2; j++)
            if (k->comment[j][0] && starts(s, n, i, k->comment[j]))
                break;
        if (j < 2) {
            paint(cls, i, n, OE_C_COMMENT);
            break;
        }
        if (k->strings[0] && strchr(k->strings, c)) {
            int st = i++;
            while (i < n && (unsigned char)s[i] != c) {
                if (k->escape && s[i] == k->escape && i + 1 < n)
                    i++;
                i++;
            }
            if (i < n)
                i++;
            paint(cls, st, i, OE_C_STRING);
            lead = 0;
            continue;
        }
        if (k->inl[0][0] && starts(s, n, i, k->inl[0])) {
            int st = i;
            i += (int)strlen(k->inl[0]);
            while (i < n && !starts(s, n, i, k->inl[1]))
                i++;
            if (i < n)
                i += (int)strlen(k->inl[1]);
            paint(cls, st, i, OE_C_COMMAND);
            lead = 0;
            continue;
        }
        if (k->tags && c == '<' && i + 1 < n && (oe_is_word(s[i + 1]) || s[i + 1] == '/' || s[i + 1] == '!')) {
            int st = i++;
            if (s[i] == '/' || s[i] == '!')
                i++;
            while (i < n && (oe_is_word(s[i]) || s[i] == '-'))
                i++;
            paint(cls, st, i, OE_C_COMMAND);
            tag = ST_TAG;
            lead = 0;
            continue;
        }
        if (lead && k->directive && c == (unsigned char)k->directive) {
            int st = i++;
            while (i < n && oe_is_word(s[i]))
                i++;
            paint(cls, st, i, OE_C_KEYWORD);
            lead = 0;
            continue;
        }
        if (k->variable && c == (unsigned char)k->variable && i + 1 < n) {
            int st = i++;
            if (s[i] == '(' || s[i] == '{') {
                int close = s[i] == '(' ? ')' : '}';
                while (i < n && s[i] != close)
                    i++;
                if (i < n)
                    i++;
            } else
                while (i < n && oe_is_word(s[i]))
                    i++;
            paint(cls, st, i, OE_C_VARIABLE);
            lead = 0;
            continue;
        }
        if (k->numbers && (is_digit(c) || (c == '$' && i + 1 < n && is_hex((unsigned char)s[i + 1]))) &&
            (i == 0 || !oe_is_word(s[i - 1]))) {
            int st = i++;
            while (i < n && (is_hex((unsigned char)s[i]) || s[i] == 'x' || s[i] == 'X' || s[i] == '.'))
                i++;
            paint(cls, st, i, OE_C_NUMBER);
            lead = 0;
            continue;
        }
        if (oe_is_word(c)) {
            int st = i, wn, cl, col0 = i == 0;
            while (i < n && oe_is_word(s[i]))
                i++;
            wn = i - st;
            if (k->suffix && i + 1 < n && s[i] == k->suffix && oe_is_word(s[i + 1])) {
                i++;
                while (i < n && oe_is_word(s[i]))
                    i++;
            }
            if (label_next) {
                cl = OE_C_LABEL;
                label_next = 0;
            } else if (k->label_colon && lead && i < n && s[i] == ':') {
                i++;
                cl = OE_C_LABEL;
            } else if (k->label_col0 && col0) {
                if (i < n && s[i] == ':')
                    i++;
                cl = OE_C_LABEL;
            } else {
                int a;
                cl = lookup(k, s + st, wn);
                for (a = 0; a < k->nafter; a++)
                    if (cmp_word(k, s + st, wn, k->after[a]) == 0)
                        label_next = 1;
            }
            paint(cls, st, i, cl);
            lead = 0;
            continue;
        }
        lead = 0;
        i++;
    }
    return depth | tag;
}

int oe_syn_state(oe_doc *d, const oe_kind *k, long line)
{
    long l;
    if (!k || line <= 0)
        return 0;
    if (line + 1 > d->lscap) {
        long nc = line + 1 + 256;
        int *ns = realloc(d->lstate, nc * sizeof(int));
        if (!ns)
            return 0;
        d->lstate = ns;
        d->lscap = nc;
    }
    if (d->lsok < 1) {
        d->lstate[0] = 0;
        d->lsok = 1;
    }
    for (l = d->lsok; l <= line; l++) {
        long st = oe_buf_line_start(&d->buf, l - 1);
        int n = (int)oe_buf_line_len(&d->buf, l - 1);
        static unsigned char scratch[1024];
        const char *s = oe_buf_span(&d->buf, st, n);
        unsigned char *cls = scratch;
        int heap = 0;
        if (n > (int)sizeof scratch) {
            if (!(cls = malloc(n)))
                return 0;
            heap = 1;
        }
        d->lstate[l] = oe_syn_line(k, s, n, d->lstate[l - 1], cls);
        if (heap)
            free(cls);
    }
    if (d->lsok < line + 1)
        d->lsok = line + 1;
    return d->lstate[line];
}
