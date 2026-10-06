/* oe_doc: one open file (oe_doc.h).
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include "oe_doc.h"
#include <stdlib.h>
#include <string.h>

int oe_is_word(int c)
{
    c &= 255;
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           (c >= 0x80 && c != 0xd7 && c != 0xf7 && c != 0xa0 && (c >= 0xc0 || c < 0xa0));
}

int oe_doc_init(oe_doc *d)
{
    memset(d, 0, sizeof *d);
    if (!oe_buf_init(&d->buf))
        return 0;
    d->goal = -1;
    d->tabw = 8;
    d->autoindent = 1;
    d->kind = -1;
    d->group_next = 1;
    return 1;
}

static void free_undo(oe_doc *d, long from)
{
    long i;
    for (i = from; i < d->un; i++)
        free(d->u[i].text);
    d->un = from;
}

void oe_doc_free(oe_doc *d)
{
    free_undo(d, 0);
    free(d->u);
    free(d->lstate);
    oe_buf_free(&d->buf);
    memset(d, 0, sizeof *d);
}

/* ---- reading and writing ---- */

static int valid_utf8(const unsigned char *s, long n, int *any)
{
    long i = 0;
    *any = 0;
    while (i < n) {
        unsigned c = s[i];
        int k;
        if (c < 0x80) {
            i++;
            continue;
        }
        *any = 1;
        if (c >= 0xc2 && c <= 0xdf)
            k = 1;
        else if (c >= 0xe0 && c <= 0xef)
            k = 2;
        else if (c >= 0xf0 && c <= 0xf4)
            k = 3;
        else
            return 0;
        while (k--) {
            i++;
            if (i >= n || (s[i] & 0xc0) != 0x80)
                return 0;
        }
        i++;
    }
    return 1;
}

int oe_doc_load(oe_doc *d, const char *data, long n)
{
    const unsigned char *s = (const unsigned char *)data;
    long crlf = 0, cr = 0, lf = 0, i, o = 0;
    int any;
    char *t;
    d->bom = 0;
    if (n >= 3 && s[0] == 0xef && s[1] == 0xbb && s[2] == 0xbf) {
        d->bom = 1;
        s += 3;
        n -= 3;
    }
    d->cs = (d->bom || (valid_utf8(s, n, &any) && any)) ? OE_CS_UTF8 : OE_CS_LATIN1;
    for (i = 0; i < n; i++) {
        if (s[i] == '\r') {
            if (i + 1 < n && s[i + 1] == '\n') {
                crlf++;
                i++;
            } else
                cr++;
        } else if (s[i] == '\n')
            lf++;
    }
    d->eol = crlf > lf && crlf >= cr ? OE_EOL_CRLF : cr > lf && cr > crlf ? OE_EOL_CR : OE_EOL_LF;
    if (!(t = malloc(n + 1)))
        return 0;
    for (i = 0; i < n; i++) {
        if (s[i] == '\r') {
            if (i + 1 < n && s[i + 1] == '\n')
                i++;
            t[o++] = '\n';
        } else
            t[o++] = (char)s[i];
    }
    oe_buf_delete(&d->buf, 0, oe_buf_len(&d->buf));
    if (!oe_buf_insert(&d->buf, 0, t, o)) {
        free(t);
        return 0;
    }
    free(t);
    free_undo(d, 0);
    d->uat = d->saved_at = 0;
    d->caret = d->anchor = 0;
    d->top = 0;
    d->left = 0;
    d->goal = -1;
    d->typing = 0;
    d->lsok = 0;
    d->edits = 0;
    return 1;
}

long oe_doc_save_size(oe_doc *d)
{
    long n = oe_buf_len(&d->buf);
    if (d->eol == OE_EOL_CRLF)
        n += oe_buf_lines(&d->buf) - 1;
    return n + (d->bom ? 3 : 0);
}

void oe_doc_save_bytes(oe_doc *d, char *out)
{
    long n = oe_buf_len(&d->buf), i;
    const char *s = oe_buf_span(&d->buf, 0, n);
    if (d->bom) {
        *out++ = (char)0xef;
        *out++ = (char)0xbb;
        *out++ = (char)0xbf;
    }
    if (d->eol == OE_EOL_LF) {
        memcpy(out, s, n);
        return;
    }
    for (i = 0; i < n; i++) {
        if (s[i] == '\n') {
            *out++ = '\r';
            if (d->eol == OE_EOL_CRLF)
                *out++ = '\n';
        } else
            *out++ = s[i];
    }
}

void oe_doc_mark_saved(oe_doc *d)
{
    d->saved_at = d->uat;
    d->typing = 0;
}

int oe_doc_modified(const oe_doc *d)
{
    return d->saved_at != d->uat;
}

long oe_doc_len(const oe_doc *d)
{
    return oe_buf_len(&d->buf);
}

long oe_doc_lines(const oe_doc *d)
{
    return oe_buf_lines(&d->buf);
}

long oe_doc_line(const oe_doc *d)
{
    return oe_buf_line_of(&d->buf, d->caret);
}

/* ---- characters ---- */

static int ch(const oe_doc *d, long pos)
{
    return oe_buf_char(&d->buf, pos);
}

long oe_doc_next(const oe_doc *d, long pos)
{
    long len = oe_buf_len(&d->buf);
    if (pos >= len)
        return len;
    pos++;
    if (d->cs == OE_CS_UTF8)
        while (pos < len && (ch(d, pos) & 0xc0) == 0x80)
            pos++;
    return pos;
}

long oe_doc_prev(const oe_doc *d, long pos)
{
    if (pos <= 0)
        return 0;
    pos--;
    if (d->cs == OE_CS_UTF8)
        while (pos > 0 && (ch(d, pos) & 0xc0) == 0x80)
            pos--;
    return pos;
}

/* A UTF-8 character at pos as ISO-8859-1, 0x7f when outside it. */
static int decode(const oe_doc *d, long pos, long *next)
{
    int c = ch(d, pos);
    *next = oe_doc_next(d, pos);
    if (d->cs != OE_CS_UTF8 || c < 0x80)
        return c;
    if ((c & 0xe0) == 0xc0 && *next == pos + 2) {
        int v = ((c & 0x1f) << 6) | (ch(d, pos + 1) & 0x3f);
        return v < 256 ? v : 0x7f;
    }
    return 0x7f;
}

int oe_doc_col(oe_doc *d, long pos)
{
    long line = oe_buf_line_of(&d->buf, pos), p = oe_buf_line_start(&d->buf, line), nx;
    int col = 0;
    while (p < pos) {
        if (ch(d, p) == '\t')
            col += d->tabw - col % d->tabw;
        else
            col++;
        nx = oe_doc_next(d, p);
        p = nx;
    }
    return col;
}

long oe_doc_pos_at(oe_doc *d, long line, int want)
{
    long p, end;
    int col = 0;
    if (line < 0)
        line = 0;
    if (line >= oe_buf_lines(&d->buf))
        line = oe_buf_lines(&d->buf) - 1;
    p = oe_buf_line_start(&d->buf, line);
    end = p + oe_buf_line_len(&d->buf, line);
    while (p < end) {
        int w = ch(d, p) == '\t' ? d->tabw - col % d->tabw : 1;
        if (col + w > want) {
            /* Past the middle of a tab goes after it. */
            if (w > 1 && want - col > w / 2)
                p = oe_doc_next(d, p);
            break;
        }
        col += w;
        p = oe_doc_next(d, p);
    }
    return p;
}

int oe_doc_show(oe_doc *d, long line, int left, int max, char *out, int *src)
{
    long start = oe_buf_line_start(&d->buf, line), end = start + oe_buf_line_len(&d->buf, line), p = start, nx;
    int col = 0, n = 0;
    while (p < end && n < max) {
        int c = decode(d, p, &nx), w = 1, k;
        if (c == '\t') {
            w = d->tabw - col % d->tabw;
            c = ' ';
        } else if (c < 32 || (c >= 0x7f && c < 0xa0))
            c = 0x7f;
        for (k = 0; k < w && n < max; k++, col++)
            if (col >= left) {
                out[n] = (char)c;
                src[n] = (int)(p - start);
                n++;
            }
        p = nx;
    }
    return n;
}

/* ---- selection and moving ---- */

int oe_doc_has_sel(const oe_doc *d)
{
    return d->caret != d->anchor;
}

void oe_doc_sel(const oe_doc *d, long *a, long *b)
{
    *a = d->caret < d->anchor ? d->caret : d->anchor;
    *b = d->caret < d->anchor ? d->anchor : d->caret;
}

char *oe_doc_text(oe_doc *d, long a, long b, long *n)
{
    char *t;
    if (b < a)
        b = a;
    if (!(t = malloc(b - a + 1)))
        return NULL;
    *n = oe_buf_copy(&d->buf, a, b - a, t);
    t[*n] = 0;
    return t;
}

static long clamp(const oe_doc *d, long pos)
{
    long len = oe_buf_len(&d->buf);
    return pos < 0 ? 0 : pos > len ? len : pos;
}

void oe_doc_set_caret(oe_doc *d, long pos, int extend)
{
    d->caret = clamp(d, pos);
    if (!extend)
        d->anchor = d->caret;
    d->goal = -1;
    d->typing = 0;
}

void oe_doc_select(oe_doc *d, long a, long b)
{
    d->anchor = clamp(d, a);
    d->caret = clamp(d, b);
    d->goal = -1;
    d->typing = 0;
}

void oe_doc_select_all(oe_doc *d)
{
    oe_doc_select(d, 0, oe_buf_len(&d->buf));
}

void oe_doc_select_word(oe_doc *d, long pos)
{
    long a = clamp(d, pos), b = a;
    while (a > 0 && oe_is_word(ch(d, a - 1)))
        a--;
    while (b < oe_buf_len(&d->buf) && oe_is_word(ch(d, b)))
        b++;
    oe_doc_select(d, a, b);
}

void oe_doc_select_line(oe_doc *d, long pos)
{
    long line = oe_buf_line_of(&d->buf, clamp(d, pos));
    oe_doc_select(d, oe_buf_line_start(&d->buf, line), oe_buf_line_start(&d->buf, line + 1));
}

void oe_doc_move(oe_doc *d, int how, int extend, int page)
{
    long p = d->caret, line = oe_buf_line_of(&d->buf, p), len = oe_buf_len(&d->buf), a, b;
    int goal = d->goal;
    /* With a selection and no Shift, left and right go to its edge. */
    if (!extend && oe_doc_has_sel(d) && (how == OE_LEFT || how == OE_RIGHT)) {
        oe_doc_sel(d, &a, &b);
        oe_doc_set_caret(d, how == OE_LEFT ? a : b, 0);
        return;
    }
    switch (how) {
    case OE_LEFT:
        p = oe_doc_prev(d, p);
        break;
    case OE_RIGHT:
        p = oe_doc_next(d, p);
        break;
    case OE_UP:
    case OE_DOWN:
    case OE_PGUP:
    case OE_PGDN: {
        long n = how == OE_UP ? -1 : how == OE_DOWN ? 1 : how == OE_PGUP ? -(page > 1 ? page - 1 : 1) : (page > 1 ? page - 1 : 1);
        if (goal < 0)
            goal = oe_doc_col(d, p);
        if (line + n < 0)
            p = 0;
        else if (line + n >= oe_buf_lines(&d->buf))
            p = len;
        else
            p = oe_doc_pos_at(d, line + n, goal);
        if (how == OE_PGUP || how == OE_PGDN) {
            d->top += n;
            if (d->top < 0)
                d->top = 0;
        }
        break;
    }
    case OE_WORDL:
        while (p > 0 && !oe_is_word(ch(d, p - 1)) && ch(d, p - 1) != '\n')
            p--;
        if (p > 0 && ch(d, p - 1) == '\n' && p == d->caret)
            p--;
        else
            while (p > 0 && oe_is_word(ch(d, p - 1)))
                p--;
        break;
    case OE_WORDR:
        if (ch(d, p) == '\n')
            p++;
        else {
            while (p < len && oe_is_word(ch(d, p)))
                p++;
            while (p < len && !oe_is_word(ch(d, p)) && ch(d, p) != '\n')
                p++;
        }
        break;
    case OE_HOME: {
        /* First to the line's first non-blank, then to its start. */
        long s = oe_buf_line_start(&d->buf, line), t = s;
        while (ch(d, t) == ' ' || ch(d, t) == '\t')
            t++;
        p = p == t ? s : t;
        break;
    }
    case OE_END:
        p = oe_buf_line_start(&d->buf, line) + oe_buf_line_len(&d->buf, line);
        break;
    case OE_TOP:
        p = 0;
        break;
    case OE_BOTTOM:
        p = len;
        break;
    }
    oe_doc_set_caret(d, p, extend);
    if (how == OE_UP || how == OE_DOWN || how == OE_PGUP || how == OE_PGDN)
        d->goal = goal;
}

/* ---- undo ---- */

static void edited(oe_doc *d, long pos)
{
    long line = oe_buf_line_of(&d->buf, pos);
    if (d->lsok > line + 1)
        d->lsok = line + 1;
    d->edits++;
}

static oe_undo *push(oe_doc *d, int ins, long pos, const char *s, long n, int typing)
{
    oe_undo *r;
    free_undo(d, d->uat);
    if (d->saved_at > d->un)
        d->saved_at = -1;
    if (d->un == d->ucap) {
        long nc = d->ucap ? d->ucap * 2 : 64;
        oe_undo *nu = realloc(d->u, nc * sizeof *nu);
        if (!nu)
            return NULL;
        d->u = nu;
        d->ucap = nc;
    }
    if (d->un >= OE_UNDO_MAX) {
        /* Forget the oldest whole step. */
        long k = 1;
        while (k < d->un && d->u[k].group == d->u[0].group)
            k++;
        {
            long i;
            for (i = 0; i < k; i++)
                free(d->u[i].text);
            memmove(d->u, d->u + k, (d->un - k) * sizeof *d->u);
            d->un -= k;
            d->uat -= k;
            d->saved_at = d->saved_at >= k ? d->saved_at - k : -1;
        }
    }
    r = &d->u[d->un];
    if (!(r->text = malloc(n > 0 ? n : 1)))
        return NULL;
    memcpy(r->text, s, n);
    r->pos = pos;
    r->n = n;
    r->ins = (unsigned char)ins;
    r->typing = (unsigned char)typing;
    r->caret = d->caret;
    r->anchor = d->anchor;
    r->group = d->group_fixed ? d->group_fixed : d->group_next++;
    d->un++;
    d->uat = d->un;
    return r;
}

/* Grows the last record when this keystroke continues it. */
static int merge(oe_doc *d, int kind, long pos, const char *s, long n)
{
    oe_undo *r;
    char *t;
    if (!d->typing || d->group_fixed || d->uat != d->un || d->uat == 0 || d->saved_at == d->uat)
        return 0;
    r = &d->u[d->uat - 1];
    if (r->typing != kind)
        return 0;
    if (kind == 1 && pos == r->pos + r->n && s[0] != '\n' && !(s[0] == ' ' && r->text[r->n - 1] != ' ')) {
        if (!(t = realloc(r->text, r->n + n)))
            return 0;
        memcpy(t + r->n, s, n);
        r->text = t;
        r->n += n;
        return 1;
    }
    if (kind == 2 && pos + n == r->pos && s[0] != '\n') {
        if (!(t = realloc(r->text, r->n + n)))
            return 0;
        memmove(t + n, t, r->n);
        memcpy(t, s, n);
        r->text = t;
        r->n += n;
        r->pos = pos;
        return 1;
    }
    if (kind == 3 && pos == r->pos && s[0] != '\n') {
        if (!(t = realloc(r->text, r->n + n)))
            return 0;
        memcpy(t + r->n, s, n);
        r->text = t;
        r->n += n;
        return 1;
    }
    return 0;
}

static int do_insert(oe_doc *d, long pos, const char *s, long n, int typing)
{
    if (n <= 0)
        return 1;
    if (!merge(d, typing, pos, s, n) && !push(d, 1, pos, s, n, typing))
        return 0;
    if (!oe_buf_insert(&d->buf, pos, s, n)) {
        /* Undo can't describe what didn't happen. */
        if (d->uat == d->un && d->un > 0 && d->u[d->un - 1].pos == pos && d->u[d->un - 1].ins) {
            free(d->u[--d->un].text);
            d->uat = d->un;
        }
        return 0;
    }
    edited(d, pos);
    return 1;
}

static int do_delete(oe_doc *d, long a, long b, int typing)
{
    char *t;
    long n;
    if (b <= a)
        return 1;
    if (!(t = oe_doc_text(d, a, b, &n)))
        return 0;
    if (!merge(d, typing, a, t, n) && !push(d, 0, a, t, n, typing)) {
        free(t);
        return 0;
    }
    free(t);
    oe_buf_delete(&d->buf, a, b - a);
    edited(d, a);
    return 1;
}

void oe_doc_begin(oe_doc *d)
{
    if (!d->nest++)
        d->group_fixed = d->group_next++;
    d->typing = 0;
}

void oe_doc_end(oe_doc *d)
{
    if (d->nest > 0 && !--d->nest)
        d->group_fixed = 0;
    d->typing = 0;
}

void oe_doc_break_typing(oe_doc *d)
{
    d->typing = 0;
}

int oe_doc_can_undo(const oe_doc *d)
{
    return d->uat > 0;
}

int oe_doc_can_redo(const oe_doc *d)
{
    return d->uat < d->un;
}

int oe_doc_undo(oe_doc *d)
{
    unsigned g;
    long c = 0, a = 0;
    if (d->readonly || !d->uat)
        return 0;
    g = d->u[d->uat - 1].group;
    while (d->uat > 0 && d->u[d->uat - 1].group == g) {
        oe_undo *r = &d->u[--d->uat];
        if (r->ins)
            oe_buf_delete(&d->buf, r->pos, r->n);
        else if (!oe_buf_insert(&d->buf, r->pos, r->text, r->n)) {
            d->uat++;
            return 0;
        }
        edited(d, r->pos);
        c = r->caret;
        a = r->anchor;
    }
    d->caret = clamp(d, c);
    d->anchor = clamp(d, a);
    d->goal = -1;
    d->typing = 0;
    return 1;
}

int oe_doc_redo(oe_doc *d)
{
    unsigned g;
    long c = d->caret;
    if (d->readonly || d->uat >= d->un)
        return 0;
    g = d->u[d->uat].group;
    while (d->uat < d->un && d->u[d->uat].group == g) {
        oe_undo *r = &d->u[d->uat];
        if (r->ins) {
            if (!oe_buf_insert(&d->buf, r->pos, r->text, r->n))
                return 0;
            c = r->pos + r->n;
        } else {
            oe_buf_delete(&d->buf, r->pos, r->n);
            c = r->pos;
        }
        edited(d, r->pos);
        d->uat++;
    }
    d->caret = d->anchor = clamp(d, c);
    d->goal = -1;
    d->typing = 0;
    return 1;
}

/* ---- editing ---- */

int oe_doc_delete_sel(oe_doc *d)
{
    long a, b;
    if (d->readonly)
        return 0;
    oe_doc_sel(d, &a, &b);
    if (a == b)
        return 1;
    if (!do_delete(d, a, b, 0))
        return 0;
    d->caret = d->anchor = a;
    d->goal = -1;
    return 1;
}

int oe_doc_insert(oe_doc *d, const char *s, long n, int typing)
{
    int sel = oe_doc_has_sel(d), ok;
    if (d->readonly)
        return 0;
    if (sel) {
        oe_doc_begin(d);
        typing = 0;
        if (!oe_doc_delete_sel(d)) {
            oe_doc_end(d);
            return 0;
        }
    }
    ok = do_insert(d, d->caret, s, n, typing ? 1 : 0);
    if (ok)
        d->caret = d->anchor = d->caret + n;
    if (sel)
        oe_doc_end(d);
    d->typing = ok && typing && !sel;
    d->goal = -1;
    return ok;
}

int oe_doc_backspace(oe_doc *d)
{
    long p;
    int ok;
    if (d->readonly)
        return 0;
    if (oe_doc_has_sel(d))
        return oe_doc_delete_sel(d);
    if (d->caret == 0)
        return 1;
    p = oe_doc_prev(d, d->caret);
    ok = do_delete(d, p, d->caret, 2);
    if (ok)
        d->caret = d->anchor = p;
    d->typing = ok;
    d->goal = -1;
    return ok;
}

int oe_doc_delete(oe_doc *d)
{
    long p;
    int ok;
    if (d->readonly)
        return 0;
    if (oe_doc_has_sel(d))
        return oe_doc_delete_sel(d);
    p = oe_doc_next(d, d->caret);
    if (p == d->caret)
        return 1;
    ok = do_delete(d, d->caret, p, 3);
    d->typing = ok;
    d->goal = -1;
    return ok;
}

int oe_doc_newline(oe_doc *d)
{
    char ind[128];
    long s, n = 1;
    int ok;
    if (d->readonly)
        return 0;
    ind[0] = '\n';
    if (d->autoindent) {
        long a = d->caret < d->anchor ? d->caret : d->anchor;
        s = oe_buf_line_start(&d->buf, oe_buf_line_of(&d->buf, a));
        while (n < (long)sizeof ind && s < a && (ch(d, s) == ' ' || ch(d, s) == '\t'))
            ind[n++] = (char)ch(d, s++);
    }
    oe_doc_begin(d);
    ok = oe_doc_insert(d, ind, n, 0);
    oe_doc_end(d);
    return ok;
}

int oe_doc_tab(oe_doc *d, int outdent)
{
    long a, b, l0, l1, l;
    int ok = 1;
    if (d->readonly)
        return 0;
    oe_doc_sel(d, &a, &b);
    l0 = oe_buf_line_of(&d->buf, a);
    l1 = oe_buf_line_of(&d->buf, b);
    if (!outdent && (a == b || l0 == l1)) {
        char sp[16];
        int k = d->spaces ? d->tabw - oe_doc_col(d, a) % d->tabw : 1;
        memset(sp, d->spaces ? ' ' : '\t', sizeof sp);
        return oe_doc_insert(d, sp, k > 16 ? 16 : k, 0);
    }
    /* A block: every line it touches moves one step. */
    if (b > a && l1 > l0 && b == oe_buf_line_start(&d->buf, l1))
        l1--;
    oe_doc_begin(d);
    for (l = l0; l <= l1 && ok; l++) {
        long s = oe_buf_line_start(&d->buf, l);
        if (!outdent) {
            if (oe_buf_line_len(&d->buf, l) == 0)
                continue;
            if (d->spaces) {
                char sp[16];
                memset(sp, ' ', sizeof sp);
                ok = do_insert(d, s, sp, d->tabw > 16 ? 16 : d->tabw, 0);
            } else
                ok = do_insert(d, s, "\t", 1, 0);
        } else {
            long e = s;
            int col = 0;
            while (col < d->tabw && (ch(d, e) == ' ' || ch(d, e) == '\t')) {
                if (ch(d, e++) == '\t')
                    break;
                col++;
            }
            ok = do_delete(d, s, e, 0);
        }
    }
    oe_doc_end(d);
    oe_doc_select(d, oe_buf_line_start(&d->buf, l0), oe_buf_line_start(&d->buf, l1 + 1));
    return ok;
}

int oe_doc_replace(oe_doc *d, long a, long b, const char *s, long n)
{
    int ok;
    if (d->readonly)
        return 0;
    oe_doc_begin(d);
    ok = do_delete(d, a, b, 0) && do_insert(d, a, s, n, 0);
    oe_doc_end(d);
    if (ok)
        oe_doc_select(d, a, a + n);
    return ok;
}
