/* oe_pat: AmigaDOS-style patterns (oe_pat.h).
 *
 * A backtracking matcher: each step matches one element and then the rest
 * of the pattern, carried as a chain of what is left to match. Lines are
 * short, so the simple way is quick enough.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include "oe_pat.h"
#include <string.h>

typedef struct cont {
    const char *p, *pe;
    const struct cont *next;
    const char *must_pass;      /* a repeat must have moved past this */
} cont;

typedef struct mctx {
    const char *se;
    int nocase;
    int anchored;               /* the match must reach se */
    int steps;
} mctx;

int oe_lower(int c)
{
    c &= 255;
    if ((c >= 'A' && c <= 'Z') || (c >= 0xc0 && c <= 0xde && c != 0xd7))
        return c + 32;
    return c;
}

/* The end of the element starting at p. */
static const char *elem_end(const char *p, const char *pe)
{
    if (p >= pe)
        return pe;
    switch (*p) {
    case '\'':
        return p + 2 <= pe ? p + 2 : pe;
    case '[':
        p++;
        if (p < pe && *p == '~')
            p++;
        if (p < pe && *p == ']')
            p++;
        while (p < pe && *p != ']') {
            if (*p == '\'' && p + 1 < pe)
                p++;
            p++;
        }
        return p < pe ? p + 1 : pe;
    case '(': {
        int depth = 0;
        for (; p < pe; p++) {
            if (*p == '\'' && p + 1 < pe) {
                p++;
                continue;
            }
            if (*p == '(')
                depth++;
            else if (*p == ')' && --depth == 0)
                return p + 1;
        }
        return pe;
    }
    case '#':
        return elem_end(p + 1, pe);
    default:
        return p + 1;
    }
}

static int same(const mctx *m, int a, int b)
{
    a &= 255;
    b &= 255;
    return a == b || (m->nocase && oe_lower(a) == oe_lower(b));
}

static int in_class(const mctx *m, const char *p, const char *ce, int c)
{
    int neg = 0, hit = 0;
    p++;                                /* past [ */
    if (p < ce && *p == '~') {
        neg = 1;
        p++;
    }
    while (p < ce) {
        int lo, hi;
        if (*p == '\'' && p + 1 < ce)
            p++;
        lo = (unsigned char)*p++;
        hi = lo;
        if (p + 1 < ce && *p == '-') {
            p++;
            if (*p == '\'' && p + 1 < ce)
                p++;
            hi = (unsigned char)*p++;
        }
        if ((c & 255) >= lo && (c & 255) <= hi)
            hit = 1;
        else if (m->nocase && oe_lower(c) >= oe_lower(lo) && oe_lower(c) <= oe_lower(hi))
            hit = 1;
    }
    return hit != neg;
}

static const char *match(mctx *m, const char *p, const char *pe, const char *s, const cont *k);

static const char *go_on(mctx *m, const char *s, const cont *k)
{
    if (!k)
        return m->anchored && s != m->se ? NULL : s;
    if (k->must_pass && s <= k->must_pass)
        return NULL;
    return match(m, k->p, k->pe, s, k->next);
}

static const char *longer(const char *a, const char *b)
{
    if (!a)
        return b;
    if (!b)
        return a;
    return a > b ? a : b;
}

static const char *match(mctx *m, const char *p, const char *pe, const char *s, const cont *k)
{
    const char *e;
    if (++m->steps > 200000)            /* a pathological pattern gives up */
        return NULL;
    if (p >= pe)
        return go_on(m, s, k);
    e = elem_end(p, pe);
    switch (*p) {
    case '?':
        return s < m->se ? match(m, e, pe, s + 1, k) : NULL;
    case '*': {
        /* #? : the longest first. */
        const char *t, *best = NULL;
        for (t = m->se; t >= s; t--)
            if ((best = match(m, e, pe, t, k)))
                return best;
        return NULL;
    }
    case '%':
        return match(m, e, pe, s, k);
    case '\'':
        if (p + 1 < pe && s < m->se && same(m, *s, p[1]))
            return match(m, e, pe, s + 1, k);
        return NULL;
    case '[':
        if (s < m->se && in_class(m, p, e - 1, (unsigned char)*s))
            return match(m, e, pe, s + 1, k);
        return NULL;
    case '(': {
        /* Each alternative, then the rest. */
        const char *a = p + 1, *best = NULL, *q;
        cont rest;
        int depth = 0;
        rest.p = e;
        rest.pe = pe;
        rest.next = k;
        rest.must_pass = NULL;
        for (q = a; q < e - 1; q++) {
            if (*q == '\'' && q + 1 < e - 1) {
                q++;
                continue;
            }
            if (*q == '(')
                depth++;
            else if (*q == ')')
                depth--;
            else if (*q == '|' && depth == 0) {
                best = longer(best, match(m, a, q, s, &rest));
                a = q + 1;
            }
        }
        return longer(best, match(m, a, e - 1, s, &rest));
    }
    case '#': {
        /* Zero times, or once more and then this again (always moving on). */
        const char *ae = e, *a = p + 1, *zero, *more;
        cont again;
        if (a < pe && *a == '?') {
            const char *t;
            for (t = m->se; t >= s; t--)
                if ((more = match(m, ae, pe, t, k)))
                    return more;
            return NULL;
        }
        zero = match(m, ae, pe, s, k);
        again.p = p;
        again.pe = pe;
        again.next = k;
        again.must_pass = s;
        more = match(m, a, ae, s, &again);
        return longer(zero, more);
    }
    default:
        if (s < m->se && same(m, *s, *p))
            return match(m, e, pe, s + 1, k);
        return NULL;
    }
}

const char *oe_pat_at(const char *pat, int pn, const char *s, const char *se, int nocase)
{
    mctx m;
    m.se = se;
    m.nocase = nocase;
    m.anchored = 0;
    m.steps = 0;
    return match(&m, pat, pat + pn, s, NULL);
}

int oe_pat_whole(const char *pat, const char *s, int nocase)
{
    int sn = (int)strlen(s);
    mctx m;
    const char *e;
    m.se = s + sn;
    m.nocase = nocase;
    m.anchored = 1;
    m.steps = 0;
    e = match(&m, pat, pat + strlen(pat), s, NULL);
    return e == s + sn;
}

int oe_pat_is_wild(const char *pat)
{
    for (; *pat; pat++)
        if (strchr("?#*[(%|'", *pat))
            return 1;
    return 0;
}

int oe_strnicmp(const char *a, const char *b, long n)
{
    for (; n > 0; n--, a++, b++) {
        int x = oe_lower((unsigned char)*a), y = oe_lower((unsigned char)*b);
        if (x != y)
            return x - y;
        if (!x)
            return 0;
    }
    return 0;
}

int oe_stricmp(const char *a, const char *b)
{
    return oe_strnicmp(a, b, 0x7fffffffL);
}
