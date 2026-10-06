/* oe_buf: gap buffer and line index (oe_buf.h).
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include "oe_buf.h"
#include <stdlib.h>
#include <string.h>

#define GAP_MIN 1024

int oe_buf_init(oe_buf *b)
{
    memset(b, 0, sizeof *b);
    b->cap = GAP_MIN;
    b->ge = b->cap;
    b->lcap = 64;
    if (!(b->b = malloc(b->cap)) || !(b->ls = malloc(b->lcap * sizeof(long)))) {
        free(b->b);
        b->b = NULL;
        return 0;
    }
    b->ls[0] = 0;
    b->nl = 1;
    return 1;
}

void oe_buf_free(oe_buf *b)
{
    free(b->b);
    free(b->ls);
    memset(b, 0, sizeof *b);
}

long oe_buf_len(const oe_buf *b)
{
    return b->cap - (b->ge - b->gs);
}

int oe_buf_char(const oe_buf *b, long pos)
{
    if (pos < 0 || pos >= oe_buf_len(b))
        return -1;
    return (unsigned char)b->b[pos < b->gs ? pos : pos + (b->ge - b->gs)];
}

long oe_buf_copy(const oe_buf *b, long pos, long n, char *dst)
{
    long len = oe_buf_len(b), k, done = 0;
    if (pos < 0)
        pos = 0;
    if (pos + n > len)
        n = len - pos;
    if (n <= 0)
        return 0;
    if (pos < b->gs) {
        k = b->gs - pos < n ? b->gs - pos : n;
        memcpy(dst, b->b + pos, k);
        done = k;
        pos += k;
    }
    if (done < n)
        memcpy(dst + done, b->b + pos + (b->ge - b->gs), n - done);
    return n;
}

static void move_gap(oe_buf *b, long pos)
{
    long gl = b->ge - b->gs;
    if (pos < b->gs)
        memmove(b->b + pos + gl, b->b + pos, b->gs - pos);
    else if (pos > b->gs)
        memmove(b->b + b->gs, b->b + b->ge, pos - b->gs);
    b->gs = pos;
    b->ge = pos + gl;
}

static int grow(oe_buf *b, long need)
{
    long len = oe_buf_len(b), ncap, tail;
    char *nb;
    if (b->ge - b->gs >= need)
        return 1;
    ncap = b->cap + need + GAP_MIN + len / 4;
    if (!(nb = realloc(b->b, ncap)))
        return 0;
    tail = b->cap - b->ge;
    memmove(nb + ncap - tail, nb + b->ge, tail);
    b->b = nb;
    b->ge = ncap - tail;
    b->cap = ncap;
    return 1;
}

int oe_buf_insert(oe_buf *b, long pos, const char *s, long n)
{
    long i, nn = 0, line, k;
    if (n <= 0)
        return 1;
    if (pos < 0)
        pos = 0;
    if (pos > oe_buf_len(b))
        pos = oe_buf_len(b);
    for (i = 0; i < n; i++)
        nn += s[i] == '\n';
    if (b->nl + nn > b->lcap) {
        long nc = b->lcap * 2 > b->nl + nn ? b->lcap * 2 : b->nl + nn + 64;
        long *nls = realloc(b->ls, nc * sizeof(long));
        if (!nls)
            return 0;
        b->ls = nls;
        b->lcap = nc;
    }
    if (!grow(b, n))
        return 0;
    move_gap(b, pos);
    memcpy(b->b + b->gs, s, n);
    b->gs += n;
    line = oe_buf_line_of(b, pos);
    for (k = line + 1; k < b->nl; k++)
        b->ls[k] += n;
    if (nn) {
        memmove(b->ls + line + 1 + nn, b->ls + line + 1, (b->nl - line - 1) * sizeof(long));
        k = line + 1;
        for (i = 0; i < n; i++)
            if (s[i] == '\n')
                b->ls[k++] = pos + i + 1;
        b->nl += nn;
    }
    return 1;
}

void oe_buf_delete(oe_buf *b, long pos, long n)
{
    long len = oe_buf_len(b), first, last, k, gone;
    if (pos < 0) {
        n += pos;
        pos = 0;
    }
    if (pos + n > len)
        n = len - pos;
    if (n <= 0)
        return;
    /* Line starts inside (pos, pos+n] go; the ones after move back. */
    first = oe_buf_line_of(b, pos) + 1;
    last = first;
    while (last < b->nl && b->ls[last] <= pos + n)
        last++;
    gone = last - first;
    for (k = last; k < b->nl; k++)
        b->ls[k] -= n;
    if (gone) {
        memmove(b->ls + first, b->ls + last, (b->nl - last) * sizeof(long));
        b->nl -= gone;
    }
    move_gap(b, pos);
    b->ge += n;
}

long oe_buf_lines(const oe_buf *b)
{
    return b->nl;
}

long oe_buf_line_of(const oe_buf *b, long pos)
{
    long lo = 0, hi = b->nl - 1;
    while (lo < hi) {
        long mid = (lo + hi + 1) / 2;
        if (b->ls[mid] <= pos)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

long oe_buf_line_start(const oe_buf *b, long line)
{
    if (line <= 0)
        return 0;
    if (line >= b->nl)
        return oe_buf_len(b);
    return b->ls[line];
}

long oe_buf_line_len(const oe_buf *b, long line)
{
    if (line < 0 || line >= b->nl)
        return 0;
    if (line + 1 < b->nl)
        return b->ls[line + 1] - 1 - b->ls[line];
    return oe_buf_len(b) - b->ls[line];
}

const char *oe_buf_span(oe_buf *b, long pos, long n)
{
    if (pos < b->gs && pos + n > b->gs)
        move_gap(b, pos + n);
    return b->b + (pos < b->gs ? pos : pos + (b->ge - b->gs));
}
