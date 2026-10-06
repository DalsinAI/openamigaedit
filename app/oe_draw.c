/* oe_draw: the window's layout and drawing. The tab row, the text area with
 * its line numbers and colours, the find bar's background and the status
 * line are drawn here; the find bar's fields and the scroller are GadTools
 * gadgets made in oe_layout.
 *
 * The text is drawn a line at a time with Text(), one call per run of the
 * same colour. A keystroke redraws only its line; scrolling moves what is
 * already drawn with ScrollRaster() and draws the lines that come in.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <graphics/gfxmacros.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/dos.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oe_app.h"
#include "oe_find.h"

#define MAX_COLS 512

static char show_buf[MAX_COLS + 1];
static int show_src[MAX_COLS + 1];
static unsigned char *cls_buf;
static long cls_cap;
#define MAX_ROWS 300
static int row_state[MAX_ROWS];     /* the colour state each row was drawn with */

static LONG pen(const char *key)
{
    return ogt_pen(&A.ctx, key);
}

/* ---- pens ---- */

static ogt_rgb mix(ogt_rgb a, ogt_rgb b, int pct)
{
    ogt_rgb o;
    o.r = (unsigned char)((a.r * (100 - pct) + b.r * pct) / 100);
    o.g = (unsigned char)((a.g * (100 - pct) + b.g * pct) / 100);
    o.b = (unsigned char)((a.b * (100 - pct) + b.b * pct) / 100);
    return o;
}

void oe_obtain_pens(void)
{
    int dark = A.ctx.mode == OGT_DARK, c, depth;
    ogt_rgb list = ogt_colour(&A.ctx, "list"), accent = ogt_colour(&A.ctx, "accent"), hit, hit_on;
    depth = (int)GetBitMapAttr(A.scr->RastPort.BitMap, BMA_DEPTH);
    A.bold_only = depth <= 3;
    A.pen_text = pen("text");
    A.pen_bg = ogt_pen_rgb(&A.ctx, list);
    A.pen_cur = ogt_pen_rgb(&A.ctx, mix(list, accent, 8));
    A.pen_sel = ogt_pen_rgb(&A.ctx, mix(list, ogt_colour(&A.ctx, "fill"), 85));
    A.pen_gut = ogt_pen_rgb(&A.ctx, ogt_colour(&A.ctx, "list.alternate"));
    A.pen_gut_t = pen("muted");
    A.pen_caret = pen("accent");
    ogt_parse_colour(dark ? "#6b5a12" : "#ffe58a", &hit);
    ogt_parse_colour(dark ? "#a07818" : "#f5b942", &hit_on);
    A.pen_hit = ogt_pen_rgb(&A.ctx, hit);
    A.pen_hit_on = ogt_pen_rgb(&A.ctx, hit_on);
    A.cpen[0] = A.pen_text;
    for (c = 1; c < OE_C_COUNT; c++)
        A.cpen[c] = A.bold_only ? A.pen_text : ogt_pen_rgb(&A.ctx, A.prefs.pen[dark][c]);
}

void oe_release_pens(void)
{
    /* The context owns them: ogt_ctx_free gives them back. */
}

/* ---- hot places ---- */

void oe_add_hot(int x, int y, int w, int h, int kind, int idx)
{
    if (A.nhot < MAX_HOT) {
        oe_hot *t = &A.hots[A.nhot++];
        t->x = x;
        t->y = y;
        t->w = w;
        t->h = h;
        t->kind = kind;
        t->idx = idx;
    }
}

int oe_hot_at(int x, int y, int *idx)
{
    int i;
    for (i = A.nhot - 1; i >= 0; i--) {
        oe_hot *t = &A.hots[i];
        if (x >= t->x && x < t->x + t->w && y >= t->y && y < t->y + t->h) {
            *idx = t->idx;
            return t->kind;
        }
    }
    return 0;
}

static void drop_hots(int kind_from, int kind_to)
{
    int i, j = 0;
    for (i = 0; i < A.nhot; i++)
        if (A.hots[i].kind < kind_from || A.hots[i].kind > kind_to)
            A.hots[j++] = A.hots[i];
    A.nhot = j;
}

/* ---- layout and gadgets ---- */

void oe_remove_gadgets(void)
{
    if (A.glist) {
        RemoveGList(A.win, A.glist, -1);
        FreeGadgets(A.glist);
        A.glist = NULL;
    }
    A.g_scroll = A.g_find = A.g_repl = A.g_case = A.g_word = A.g_pat = A.g_alltabs = A.g_count = NULL;
}

static int text_w(const char *s)
{
    struct RastPort *rp = A.win->RPort;
    SetFont(rp, A.ui_font);
    return TextLength(rp, (STRPTR)s, (UWORD)strlen(s));
}

static struct Gadget *mk(int kind, struct Gadget *prev, struct NewGadget *ng, int id, const char *label,
                         int x, int y, int w, int h, ULONG flags, ULONG tag1, ...)
{
    ng->ng_LeftEdge = x;
    ng->ng_TopEdge = y;
    ng->ng_Width = w;
    ng->ng_Height = h;
    ng->ng_GadgetText = (STRPTR)label;
    ng->ng_GadgetID = id;
    ng->ng_Flags = flags;
    /* The tags into an array: no guessing how the compiler lays out "...". */
    struct TagItem tags[16];
    va_list ap;
    int i = 0;
    va_start(ap, tag1);
    for (; tag1 != TAG_DONE && i < 15; i++) {
        tags[i].ti_Tag = tag1;
        tags[i].ti_Data = va_arg(ap, ULONG);
        tag1 = va_arg(ap, ULONG);
    }
    va_end(ap);
    tags[i].ti_Tag = TAG_DONE;
    tags[i].ti_Data = 0;
    return CreateGadgetA(kind, prev, ng, tags);
}

static int digits(long n)
{
    int d = 1;
    while (n >= 10) {
        n /= 10;
        d++;
    }
    return d;
}

void oe_layout(void)
{
    struct Window *w = A.win;
    struct NewGadget ng;
    struct Gadget *g;
    int x0 = w->BorderLeft, y0 = w->BorderTop, ww = w->Width - w->BorderLeft - w->BorderRight;
    int wh = w->Height - w->BorderTop - w->BorderBottom, sw = A.fh + 6, bh = A.fh + 6, top, bottom;
    oe_doc *d = cur_doc();

    oe_remove_gadgets();
    if (sw < 14)
        sw = 14;
    A.tab_y = y0 + 2;
    A.tab_h = A.fh + 6;
    A.stat_h = A.fh + 5;
    A.stat_y = y0 + wh - A.stat_h;
    A.find_h = A.find_on ? 2 * bh + 12 : 0;
    A.find_y = A.stat_y - A.find_h;
    top = A.tab_y + A.tab_h + 1;
    bottom = A.find_y;
    A.gut_x = x0;
    A.gut_w = A.prefs.numbers ? (digits(d ? oe_doc_lines(d) : 1) < 3 ? 3 : digits(d ? oe_doc_lines(d) : 1)) * A.cw + 10 : 0;
    A.tx_x = x0 + A.gut_w + 4;
    A.tx_w = x0 + ww - sw - A.tx_x - 2;
    A.tx_y = top + 2;
    A.tx_h = bottom - top;
    A.rows = (bottom - A.tx_y - 1) / A.lh;
    if (A.rows < 1)
        A.rows = 1;
    if (A.rows > MAX_ROWS)
        A.rows = MAX_ROWS;
    A.cols = A.tx_w / (A.cw > 0 ? A.cw : 8);
    if (A.cols > MAX_COLS - 1)
        A.cols = MAX_COLS - 1;
    if (A.cols < 1)
        A.cols = 1;

    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &A.ta;
    ng.ng_VisualInfo = A.vi;
    g = CreateContext(&A.glist);
    g = A.g_scroll = mk(SCROLLER_KIND, g, &ng, G_SCROLL, NULL, x0 + ww - sw, top, sw, bottom - top, 0,
                        GTSC_Top, d ? d->top : 0, GTSC_Total, d ? oe_doc_lines(d) : 1, GTSC_Visible, A.rows,
                        GTSC_Arrows, A.fh + 2, PGA_Freedom, LORIENT_VERT, GA_RelVerify, TRUE, GA_Immediate, TRUE,
                        TAG_DONE);
    if (A.find_on && g) {
        int lw = text_w("Replace") + 10, y1 = A.find_y + 5, y2 = y1 + bh + 4, x = x0 + 6 + lw, right = x0 + ww - 6;
        int bw_prev = text_w("Prev") + 16, bw_next = text_w("Next") + 16, bw_close = text_w("Close") + 16;
        int cw_case = A.fh + 8 + text_w("Case"), cw_word = A.fh + 8 + text_w("Words"), cw_pat = A.fh + 8 + text_w("Pattern");
        int cnt_w = text_w("999 of 999") + 8, cb = A.fh + 2;
        int rest1 = bw_prev + bw_next + cnt_w + cw_case + cw_word + cw_pat + bw_close + 7 * 6;
        int bw_r = text_w("Replace") + 16, bw_all = text_w("All") + 16, cw_all = A.fh + 8 + text_w("All tabs");
        int strw = right - x - rest1, cx;
        if (strw < 80)
            strw = 80;
        g = A.g_find = mk(STRING_KIND, g, &ng, G_FIND, "Find", x, y1, strw, bh, PLACETEXT_LEFT,
                          GTST_String, (ULONG)A.find_text, GTST_MaxChars, sizeof A.find_text - 1, TAG_DONE);
        cx = x + strw + 6;
        g = mk(BUTTON_KIND, g, &ng, G_PREV, "Prev", cx, y1, bw_prev, bh, 0, TAG_DONE);
        cx += bw_prev + 4;
        g = mk(BUTTON_KIND, g, &ng, G_NEXT, "Next", cx, y1, bw_next, bh, 0, TAG_DONE);
        cx += bw_next + 6;
        g = A.g_count = mk(TEXT_KIND, g, &ng, G_COUNT, NULL, cx, y1, cnt_w, bh, 0, GTTX_Text, (ULONG)"",
                           GTTX_Justification, GTJ_CENTER, GTTX_CopyText, TRUE, TAG_DONE);
        cx += cnt_w + 6;
        g = A.g_case = mk(CHECKBOX_KIND, g, &ng, G_CASE, "Case", cx, y1 + (bh - cb) / 2, cb, cb, PLACETEXT_RIGHT,
                          GTCB_Checked, A.f_case, GTCB_Scaled, TRUE, TAG_DONE);
        cx += cw_case + 6;
        g = A.g_word = mk(CHECKBOX_KIND, g, &ng, G_WORD, "Words", cx, y1 + (bh - cb) / 2, cb, cb, PLACETEXT_RIGHT,
                          GTCB_Checked, A.f_word, GTCB_Scaled, TRUE, TAG_DONE);
        cx += cw_word + 6;
        g = A.g_pat = mk(CHECKBOX_KIND, g, &ng, G_PAT, "Pattern", cx, y1 + (bh - cb) / 2, cb, cb, PLACETEXT_RIGHT,
                         GTCB_Checked, A.f_pat, GTCB_Scaled, TRUE, TAG_DONE);
        g = mk(BUTTON_KIND, g, &ng, G_CLOSE, "Close", right - bw_close, y1, bw_close, bh, 0, TAG_DONE);
        g = A.g_repl = mk(STRING_KIND, g, &ng, G_REPL, "Replace", x, y2, strw, bh, PLACETEXT_LEFT,
                          GTST_String, (ULONG)A.repl_text, GTST_MaxChars, sizeof A.repl_text - 1, TAG_DONE);
        cx = x + strw + 6;
        g = mk(BUTTON_KIND, g, &ng, G_REPL1, "Replace", cx, y2, bw_r, bh, 0, TAG_DONE);
        cx += bw_r + 4;
        g = mk(BUTTON_KIND, g, &ng, G_REPLALL, "All", cx, y2, bw_all, bh, 0, TAG_DONE);
        cx += bw_all + 10;
        g = A.g_alltabs = mk(CHECKBOX_KIND, g, &ng, G_ALLTABS, "All tabs", cx, y2 + (bh - cb) / 2, cb, cb,
                             PLACETEXT_RIGHT, GTCB_Checked, A.f_alltabs, GTCB_Scaled, TRUE, TAG_DONE);
        (void)cw_all;
    }
    if (g)
        AddGList(w, A.glist, (UWORD)~0, -1, NULL);
    else {
        /* Out of memory: no gadgets rather than half of them. */
        FreeGadgets(A.glist);
        A.glist = NULL;
        A.g_scroll = A.g_find = A.g_repl = A.g_case = A.g_word = A.g_pat = A.g_alltabs = A.g_count = NULL;
    }
    if (d)
        oe_show_caret();
}

/* ---- the tab row ---- */

static const char *tab_name(oe_tab *t)
{
    if (t->d.path[0])
        return (const char *)FilePart((STRPTR)t->d.path);
    return t->d.name;
}

void oe_draw_tabs(void)
{
    struct RastPort *rp = A.win->RPort;
    int x0 = A.win->BorderLeft, ww = A.win->Width - A.win->BorderLeft - A.win->BorderRight;
    int x = x0 + 6, y = A.tab_y, th = A.tab_h, right = x0 + ww - 4, i, plus_w = A.fh + 10;
    LONG line = pen("group.line");
    SetFont(rp, A.ui_font);
    drop_hots(HOT_TAB, HOT_TABPLUS);
    ogt_fill(&A.ctx, rp, "window", x0, A.win->BorderTop, ww, y + th + 1 - A.win->BorderTop);
    if (A.cur < A.first_tab)
        A.first_tab = A.cur;
    for (;;) {
        int need = 0;
        for (i = A.first_tab; i <= A.cur; i++) {
            int tw = ogt_text_width(rp, tab_name(A.tabs[i])) + A.fh + 20;
            need += (tw > 180 ? 180 : tw) + 2;
        }
        if (need + plus_w <= right - x || A.first_tab >= A.cur)
            break;
        A.first_tab++;
    }
    ogt_hline(rp, line, x0, y + th, ww);
    for (i = A.first_tab; i < A.ntabs; i++) {
        oe_tab *t = A.tabs[i];
        const char *name = tab_name(t);
        int tw = ogt_text_width(rp, name) + A.fh + 20, sel = i == A.cur, ty = sel ? y : y + 2;
        int cx, cy, s, k;
        if (tw > 180)
            tw = 180;
        if (x + tw > right - plus_w)
            break;
        ogt_fill(&A.ctx, rp, sel ? "list" : "tab", x, ty, tw, y + th - ty + (sel ? 1 : 0));
        ogt_vline(rp, line, x, ty, y + th - ty);
        ogt_vline(rp, line, x + tw - 1, ty, y + th - ty);
        ogt_hline(rp, line, x, ty, tw);
        ogt_bold(rp, sel);
        ogt_text(rp, sel ? pen("text") : pen("tab.text"), x + 8, ty + (th - (ty - y) - A.fh) / 2, name, tw - A.fh - 16);
        ogt_bold(rp, 0);
        /* Changed: a dot; else the close cross. Either closes the tab. */
        cx = x + tw - A.fh / 2 - 7;
        cy = ty + (th - (ty - y)) / 2;
        s = A.fh / 4;
        if (oe_doc_modified(&t->d))
            ogt_fill_circle(rp, pen("accent"), cx, cy, s + 1);
        else
            for (k = -s; k <= s; k++) {
                ogt_box(rp, pen("muted"), cx + k, cy + k, 1, 1);
                ogt_box(rp, pen("muted"), cx + k, cy - k, 1, 1);
            }
        oe_add_hot(cx - s - 3, cy - s - 3, 2 * s + 7, 2 * s + 7, HOT_TABCLOSE, i);
        oe_add_hot(x, ty, tw, th, HOT_TAB, i);
        x += tw + 2;
    }
    ogt_fill(&A.ctx, rp, "tab", x, y + 2, plus_w, th - 2);
    ogt_frame(rp, line, x, y + 2, plus_w, th - 1);
    ogt_bold(rp, 1);
    ogt_text(rp, pen("tab.text"), x + (plus_w - ogt_text_width(rp, "+")) / 2, y + 2 + (th - 2 - A.fh) / 2, "+", 0);
    ogt_bold(rp, 0);
    oe_add_hot(x, y + 2, plus_w, th - 2, HOT_TABPLUS, 0);
}

/* ---- the text ---- */

static unsigned char *classes_for(oe_doc *d, long line, long *n)
{
    long st = oe_buf_line_start(&d->buf, line);
    const oe_kind *k = d->kind >= 0 && d->kind < A.nkinds && A.prefs.colours ? &A.kinds[d->kind] : NULL;
    const char *s;
    *n = oe_buf_line_len(&d->buf, line);
    if (*n + 1 > cls_cap) {
        unsigned char *nb = realloc(cls_buf, *n + 256);
        if (!nb)
            return NULL;
        cls_buf = nb;
        cls_cap = *n + 256;
    }
    if (!k) {
        memset(cls_buf, OE_C_TEXT, *n);
        return cls_buf;
    }
    {
        int state = oe_syn_state(d, k, line);
        if (line - d->top >= 0 && line - d->top < MAX_ROWS)
            row_state[line - d->top] = state;
        s = oe_buf_span(&d->buf, st, *n);
        oe_syn_line(k, s, (int)*n, state, cls_buf);
    }
    return cls_buf;
}

static void draw_row(oe_doc *d, int row)
{
    struct RastPort *rp = A.win->RPort;
    long line = d->top + row, lines = oe_doc_lines(d), cline = oe_doc_line(d), sa, sb, st;
    int y = A.tx_y + row * A.lh, n = 0, i, x;
    LONG bg = A.pen_bg;
    long hs[32], he[32];
    int nh = 0;
    unsigned char *cls = NULL;
    long ln = 0;

    if (row < 0 || row >= A.rows)
        return;
    if (line == cline && A.prefs.curline && !oe_doc_has_sel(d))
        bg = A.pen_cur;
    /* The gutter. */
    if (A.gut_w) {
        ogt_box(rp, A.pen_gut, A.gut_x, y, A.gut_w, A.lh);
        if (line < lines) {
            char num[24];
            int l = sprintf(num, "%ld", line + 1), tw;
            SetFont(rp, A.tx_font);
            tw = TextLength(rp, (STRPTR)num, l);
            ogt_set_apen(rp, line == cline ? A.pen_text : A.pen_gut_t);
            SetDrMd(rp, JAM1);
            SetSoftStyle(rp, line == cline ? FSF_BOLD : 0, FSF_BOLD);
            Move(rp, A.gut_x + A.gut_w - 6 - tw, y + rp->TxBaseline);
            Text(rp, (STRPTR)num, l);
            SetSoftStyle(rp, 0, FSF_BOLD);
        }
    }
    ogt_box(rp, bg, A.gut_x + A.gut_w, y, A.tx_x + A.tx_w - (A.gut_x + A.gut_w), A.lh);
    if (line >= lines)
        return;
    SetFont(rp, A.tx_font);
    SetDrMd(rp, JAM1);
    st = oe_buf_line_start(&d->buf, line);
    n = oe_doc_show(d, line, d->left, A.cols + 1, show_buf, show_src);
    cls = classes_for(d, line, &ln);
    oe_doc_sel(d, &sa, &sb);
    if (A.find_on && A.find_text[0])
        nh = oe_find_line(d, A.find_text, (A.f_case ? OE_F_CASE : 0) | (A.f_word ? OE_F_WORD : 0) | (A.f_pat ? OE_F_PAT : 0),
                          line, hs, he, 32);
    /* Backgrounds: selection, then find matches. */
    x = A.tx_x;
    for (i = 0; i < n;) {
        long p = st + show_src[i];
        int sel = p >= sa && p < sb, hit = 0, j, w, k;
        for (k = 0; k < nh; k++)
            if (show_src[i] >= hs[k] && show_src[i] < he[k])
                hit = 1;
        j = i + 1;
        while (j < n) {
            long q = st + show_src[j];
            int sel2 = q >= sa && q < sb, hit2 = 0;
            for (k = 0; k < nh; k++)
                if (show_src[j] >= hs[k] && show_src[j] < he[k])
                    hit2 = 1;
            if (sel2 != sel || hit2 != hit)
                break;
            j++;
        }
        w = TextLength(rp, (STRPTR)show_buf + i, j - i);
        if (sel)
            ogt_box(rp, A.pen_sel, x, y, w, A.lh);
        else if (hit)
            ogt_box(rp, A.pen_hit, x, y, w, A.lh);
        x += w;
        i = j;
    }
    /* A selection running on past the line's end shows its newline. */
    if (sb > st + ln && sa <= st + ln && x < A.tx_x + A.tx_w)
        ogt_box(rp, A.pen_sel, x, y, A.cw, A.lh);
    /* The text, a run per colour. */
    x = A.tx_x;
    for (i = 0; i < n;) {
        int c = cls && show_src[i] < ln ? cls[show_src[i]] : OE_C_TEXT, j = i + 1, w;
        while (j < n && (cls && show_src[j] < ln ? cls[show_src[j]] : OE_C_TEXT) == c)
            j++;
        w = TextLength(rp, (STRPTR)show_buf + i, j - i);
        {
            int bold = A.bold_only ? (c == OE_C_KEYWORD || c == OE_C_COMMAND || c == OE_C_LABEL) : 0;
            ogt_set_apen(rp, A.cpen[c]);
            if (bold)
                SetSoftStyle(rp, FSF_BOLD, FSF_BOLD);
            if (c == OE_C_COMMENT && !A.bold_only)
                SetSoftStyle(rp, FSF_ITALIC, FSF_ITALIC);
            Move(rp, x, y + rp->TxBaseline);
            Text(rp, (STRPTR)show_buf + i, j - i);
            SetSoftStyle(rp, 0, FSF_BOLD | FSF_ITALIC);
        }
        x += w;
        i = j;
    }
    /* The caret. */
    if (line == cline && d->caret >= st && d->caret <= st + ln) {
        int col = oe_doc_col(d, d->caret) - d->left;
        if (col >= 0 && col <= A.cols) {
            int cx = A.tx_x;
            if (col > 0) {
                /* show_buf holds the columns from left. */
                int upto = col < n ? col : n;
                cx += TextLength(rp, (STRPTR)show_buf, upto) + (col - upto) * A.cw;
            }
            if (A.active)
                ogt_box(rp, A.pen_caret, cx - 1, y, 2, A.lh);
            else
                ogt_box(rp, A.pen_gut_t, cx, y, 1, A.lh);
        }
    }
}

void oe_draw_lines(long from, long to)
{
    oe_doc *d = cur_doc();
    long l;
    if (!d)
        return;
    if (from < d->top)
        from = d->top;
    if (to > d->top + A.rows - 1)
        to = d->top + A.rows - 1;
    for (l = from; l <= to; l++)
        draw_row(d, (int)(l - d->top));
}

void oe_draw_restate(long line)
{
    oe_doc *d = cur_doc();
    const oe_kind *k;
    long l;
    if (!d || d->kind < 0 || d->kind >= A.nkinds || !A.prefs.colours)
        return;
    k = &A.kinds[d->kind];
    for (l = line + 1; l < d->top + A.rows && l < oe_doc_lines(d); l++) {
        int r = (int)(l - d->top);
        if (r < 0 || r >= MAX_ROWS)
            continue;
        if (oe_syn_state(d, k, l) != row_state[r])
            draw_row(d, r);
    }
}

void oe_draw_text(void)
{
    struct RastPort *rp = A.win->RPort;
    oe_doc *d = cur_doc();
    int r, last_y;
    if (!d)
        return;
    for (r = 0; r < A.rows; r++)
        draw_row(d, r);
    /* What is left below the last whole row, and the margin above the first. */
    last_y = A.tx_y + A.rows * A.lh;
    ogt_box(rp, A.pen_gut, A.gut_x, A.tx_y - 2, A.gut_w, 2);
    ogt_box(rp, A.pen_bg, A.gut_x + A.gut_w, A.tx_y - 2, A.tx_x + A.tx_w - A.gut_x - A.gut_w, 2);
    if (A.gut_w)
        ogt_box(rp, A.pen_gut, A.gut_x, last_y, A.gut_w, A.tx_y - 2 + A.tx_h - last_y);
    ogt_box(rp, A.pen_bg, A.gut_x + A.gut_w, last_y, A.tx_x + A.tx_w - A.gut_x - A.gut_w, A.tx_y - 2 + A.tx_h - last_y);
    if (A.gut_w)
        ogt_vline(rp, pen("group.line"), A.gut_x + A.gut_w - 1, A.tx_y - 2, A.tx_h);
    ogt_box(rp, A.pen_bg, A.tx_x + A.tx_w, A.tx_y - 2, A.g_scroll ? A.g_scroll->LeftEdge - A.tx_x - A.tx_w : 2, A.tx_h);
}

void oe_update_scroller(void)
{
    oe_doc *d = cur_doc();
    if (A.g_scroll && d)
        GT_SetGadgetAttrs(A.g_scroll, A.win, NULL, GTSC_Top, d->top, GTSC_Total, oe_doc_lines(d),
                          GTSC_Visible, A.rows, TAG_DONE);
}

void oe_scroll_to(long top)
{
    oe_doc *d = cur_doc();
    long max, delta;
    if (!d)
        return;
    max = oe_doc_lines(d) - A.rows;
    if (top > max)
        top = max;
    if (top < 0)
        top = 0;
    delta = top - d->top;
    if (!delta)
        return;
    d->top = top;
    if (delta > -A.rows && delta < A.rows) {
        struct RastPort *rp = A.win->RPort;
        int x1 = A.gut_x, x2 = A.tx_x + A.tx_w - 1, y1 = A.tx_y, y2 = A.tx_y + A.rows * A.lh - 1;
        ScrollRaster(rp, 0, (WORD)(delta * A.lh), x1, y1, x2, y2);
        if (delta > 0)
            oe_draw_lines(top + A.rows - delta, top + A.rows - 1);
        else
            oe_draw_lines(top, top - delta - 1);
    } else
        oe_draw_text();
    oe_update_scroller();
}

int oe_caret_in_view(void)
{
    oe_doc *d = cur_doc();
    long line = oe_doc_line(d);
    int col = oe_doc_col(d, d->caret);
    return line >= d->top && line < d->top + A.rows && col >= d->left && col <= d->left + A.cols - 1;
}

void oe_show_caret(void)
{
    oe_doc *d = cur_doc();
    long line, top;
    int col, margin = A.cols > 16 ? 4 : 0;
    if (!d)
        return;
    line = oe_doc_line(d);
    top = d->top;
    if (line < top)
        top = line;
    else if (line >= top + A.rows)
        top = line - A.rows + 1;
    if (top > oe_doc_lines(d) - A.rows)
        top = oe_doc_lines(d) - A.rows;
    if (top < 0)
        top = 0;
    d->top = top;
    col = oe_doc_col(d, d->caret);
    if (col < d->left)
        d->left = col - margin > 0 ? col - margin : 0;
    else if (col > d->left + A.cols - 1)
        d->left = col - A.cols + 1 + margin;
}

long oe_pos_at_xy(int x, int y)
{
    struct RastPort *rp = A.win->RPort;
    oe_doc *d = cur_doc();
    long line;
    int n, i, cx;
    if (y < A.tx_y)
        line = d->top - 1;
    else
        line = d->top + (y - A.tx_y) / A.lh;
    if (line < 0)
        return 0;
    if (line >= oe_doc_lines(d))
        return oe_doc_len(d);
    SetFont(rp, A.tx_font);
    n = oe_doc_show(d, line, d->left, A.cols + 1, show_buf, show_src);
    cx = A.tx_x;
    for (i = 0; i < n; i++) {
        int w = TextLength(rp, (STRPTR)show_buf + i, 1);
        if (x < cx + w / 2)
            break;
        cx += w;
    }
    if (i < n) {
        /* Inside a tab's spaces: before the tab or after it. */
        long p = oe_buf_line_start(&d->buf, line) + show_src[i];
        if (i > 0 && show_src[i - 1] == show_src[i])
            p = oe_doc_next(d, p);
        return p;
    }
    if (n == 0 && d->left > 0)
        return oe_doc_pos_at(d, line, d->left);
    return oe_buf_line_start(&d->buf, line) + oe_buf_line_len(&d->buf, line);
}

/* ---- the status line ---- */

static const char *const cs_name[2] = { "ISO-8859-1", "UTF-8" };
static const char *const eol_name[3] = { "LF", "CR LF", "CR" };

void oe_draw_status(void)
{
    struct RastPort *rp = A.win->RPort;
    oe_tab *t = cur_tab();
    oe_doc *d = cur_doc();
    int x0 = A.win->BorderLeft, ww = A.win->Width - A.win->BorderLeft - A.win->BorderRight, x = x0 + 4, i;
    int y = A.stat_y, ty = y + (A.stat_h - A.fh) / 2 + 1;
    LONG line = pen("group.line"), tp = pen("label");
    char seg[6][48], right[40];
    int kinds[6] = { 0, 0, HOT_ST_KIND, HOT_ST_CS, HOT_ST_EOL, HOT_ST_INS };
    SetFont(rp, A.ui_font);
    drop_hots(HOT_ST_KIND, HOT_ST_INS);
    ogt_fill(&A.ctx, rp, "list.header", x0, y, ww, A.stat_h);
    ogt_hline(rp, line, x0, y, ww);
    if (!d)
        return;
    sprintf(seg[0], "Line %ld, Col %d", oe_doc_line(d) + 1, oe_doc_col(d, d->caret) + 1);
    sprintf(seg[1], "%ld line%s", oe_doc_lines(d), oe_doc_lines(d) == 1 ? "" : "s");
    sprintf(seg[2], "%s", d->kind >= 0 && d->kind < A.nkinds ? A.kinds[d->kind].name : "Plain text");
    sprintf(seg[3], "%s", cs_name[d->cs & 1]);
    sprintf(seg[4], "%s", eol_name[d->eol % 3]);
    sprintf(seg[5], "%s", t->overwrite ? "Overwrite" : "Insert");
    for (i = 0; i < 6; i++) {
        int w = ogt_text_width(rp, seg[i]);
        ogt_text(rp, tp, x + 6, ty, seg[i], 0);
        if (kinds[i])
            oe_add_hot(x, y, w + 12, A.stat_h, kinds[i], 0);
        x += w + 12;
        ogt_vline(rp, line, x, y + 3, A.stat_h - 6);
    }
    if (d->readonly)
        strcpy(right, "Read only");
    else if (oe_doc_modified(d))
        strcpy(right, t->saved_secs ? "Changed" : "Not saved yet");
    else if (t->saved_secs) {
        long m = (t->saved_secs / 60) % (24 * 60);
        sprintf(right, "Saved %02ld:%02ld", m / 60, m % 60);
    } else
        right[0] = 0;
    if (right[0])
        ogt_text(rp, tp, x0 + ww - 8 - ogt_text_width(rp, right), ty, right, 0);
}

void oe_count_hits(void)
{
    oe_doc *d = cur_doc();
    static char buf[32];            /* GadTools keeps the pointer, not a copy */
    long a, b;
    if (!A.g_count || !d)
        return;
    A.hit_total = A.hit_index = 0;
    buf[0] = 0;
    if (A.find_text[0]) {
        oe_doc_sel(d, &a, &b);
        A.hit_total = oe_find_count(d, A.find_text,
                                    (A.f_case ? OE_F_CASE : 0) | (A.f_word ? OE_F_WORD : 0) | (A.f_pat ? OE_F_PAT : 0),
                                    a, &A.hit_index);
        if (!A.hit_total)
            strcpy(buf, "Not found");
        else if (A.hit_index && b > a)
            sprintf(buf, "%ld of %ld", A.hit_index, A.hit_total);
        else
            sprintf(buf, "%ld found", A.hit_total);
    }
    GT_SetGadgetAttrs(A.g_count, A.win, NULL, GTTX_Text, (ULONG)buf, TAG_DONE);
}

void oe_draw_all(void)
{
    struct Window *w = A.win;
    int x0 = w->BorderLeft, ww = w->Width - w->BorderLeft - w->BorderRight;
    if (A.find_on) {
        ogt_fill(&A.ctx, w->RPort, "window", x0, A.find_y, ww, A.find_h);
        ogt_hline(w->RPort, pen("group.line"), x0, A.find_y, ww);
    }
    if (A.glist)
        RefreshGList(A.glist, w, NULL, -1);
    GT_RefreshWindow(w, NULL);
    oe_draw_tabs();
    oe_draw_text();
    oe_draw_status();
    if (A.find_on)
        oe_count_hits();
}
