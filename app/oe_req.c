/* oe_req: OpenEdit's small windows: a question with a field (Go to line),
 * the unsaved-changes list, and Settings. GadTools, on the window's screen,
 * in the theme's font; each runs until answered.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <exec/lists.h>
#include <exec/nodes.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/asl.h>
#include <proto/dos.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oe_app.h"

extern struct Library *AslBase;

enum { G_STRING = 1, G_OK, G_CANCEL, G_NOSAVE, G_SAVE, G_USE, G_SCHEME, G_FONT, G_FONTBTN, G_TABS, G_SPACES,
       G_INDENT, G_NUMBERS, G_CURLINE, G_COLOURS, G_LIST, G_R, G_G, G_B, G_TICK0 = 100 };

static void centre_on(int w, int h, int *x, int *y)
{
    struct Window *p = A.win;
    *x = p->LeftEdge + (p->Width - w) / 2;
    *y = p->TopEdge + (p->Height - h) / 3;
    if (*x + w > A.scr->Width)
        *x = A.scr->Width - w;
    if (*y + h > A.scr->Height)
        *y = A.scr->Height - h;
    if (*x < 0)
        *x = 0;
    if (*y < 0)
        *y = 0;
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

static int tw(const char *s)
{
    struct RastPort rp;
    InitRastPort(&rp);
    SetFont(&rp, A.ui_font);
    return TextLength(&rp, (STRPTR)s, (UWORD)strlen(s));
}

static struct Window *open_req(const char *title, int w, int h, struct Gadget *glist, ULONG idcmp)
{
    int x, y;
    struct Window *win;
    centre_on(w, h, &x, &y);
    win = OpenWindowTags(NULL, WA_Left, x, WA_Top, y, WA_Width, w, WA_Height, h, WA_Title, (ULONG)title,
                         WA_CustomScreen, (ULONG)A.scr, WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE,
                         WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_RMBTrap, TRUE,
                         WA_SmartRefresh, TRUE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | idcmp, TAG_DONE);
    if (win) {
        SetFont(win->RPort, A.ui_font);
        ogt_fill(&A.ctx, win->RPort, "window", win->BorderLeft, win->BorderTop,
                 w - win->BorderLeft - win->BorderRight, h - win->BorderTop - win->BorderBottom);
        RefreshGList(glist, win, NULL, -1);
        GT_RefreshWindow(win, NULL);
    }
    return win;
}

/* The main window waits while a requester is up. */
static struct Requester sleeper;
static void sleep_main(int on)
{
    if (on) {
        InitRequester(&sleeper);
        Request(&sleeper, A.win);
        SetWindowPointer(A.win, WA_BusyPointer, TRUE, TAG_DONE);
    } else {
        EndRequest(&sleeper, A.win);
        SetWindowPointer(A.win, TAG_DONE);
    }
}

int oe_req_three(const char *title, const char *text, const char *gads)
{
    struct EasyStruct es;
    es.es_StructSize = sizeof es;
    es.es_Flags = 0;
    es.es_Title = (UBYTE *)title;
    es.es_TextFormat = (UBYTE *)"%s";
    es.es_GadgetFormat = (UBYTE *)gads;
    return (int)EasyRequest(A.win, &es, NULL, (ULONG)text);
}

int oe_req_string(const char *title, const char *question, const char *ok, char *buf, int size, int numeric)
{
    struct Gadget *glist = NULL, *g, *str;
    struct NewGadget ng;
    struct Window *win;
    int fh = A.fh, bh = fh + 6, pad = 12, w = 340, h, top, result = 0, done = 0, bw = tw(ok) + 24;
    top = A.scr->WBorTop + A.scr->Font->ta_YSize + 1 + pad;
    h = top + fh + 6 + bh + 10 + bh + pad;
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &A.ta;
    ng.ng_VisualInfo = A.vi;
    g = CreateContext(&glist);
    if (numeric)
        g = str = mk(INTEGER_KIND, g, &ng, G_STRING, NULL, pad, top + fh + 6, w - 2 * pad, bh, 0,
                     GTIN_Number, atol(buf), GTIN_MaxChars, 9, TAG_DONE);
    else
        g = str = mk(STRING_KIND, g, &ng, G_STRING, NULL, pad, top + fh + 6, w - 2 * pad, bh, 0,
                     GTST_String, (ULONG)buf, GTST_MaxChars, size - 1, TAG_DONE);
    g = mk(BUTTON_KIND, g, &ng, G_CANCEL, "Cancel", pad, top + fh + 6 + bh + 10, tw("Cancel") + 24, bh, 0, TAG_DONE);
    g = mk(BUTTON_KIND, g, &ng, G_OK, ok, w - pad - bw, top + fh + 6 + bh + 10, bw, bh, 0, TAG_DONE);
    if (!g || !(win = open_req(title, w, h, glist, BUTTONIDCMP | STRINGIDCMP | INTEGERIDCMP))) {
        FreeGadgets(glist);
        return 0;
    }
    sleep_main(1);
    ogt_text(win->RPort, ogt_pen(&A.ctx, "text"), pad, top, question, w - 2 * pad);
    ActivateGadget(str, win, NULL);
    while (!done) {
        struct IntuiMessage *m;
        WaitPort(win->UserPort);
        while ((m = GT_GetIMsg(win->UserPort))) {
            ULONG cl = m->Class;
            UWORD code = m->Code;
            struct Gadget *gad = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            if (cl == IDCMP_REFRESHWINDOW) {
                GT_BeginRefresh(win);
                GT_EndRefresh(win, TRUE);
            } else if (cl == IDCMP_CLOSEWINDOW || (cl == IDCMP_VANILLAKEY && code == 27))
                done = 1;
            else if (cl == IDCMP_GADGETUP && (gad->GadgetID == G_OK || gad->GadgetID == G_STRING)) {
                if (numeric) {
                    LONG v = 0;
                    GT_GetGadgetAttrs(str, win, NULL, GTIN_Number, (ULONG)&v, TAG_DONE);
                    snprintf(buf, size, "%ld", (long)v);
                    result = 1;
                } else {
                    STRPTR s = NULL;
                    GT_GetGadgetAttrs(str, win, NULL, GTST_String, (ULONG)&s, TAG_DONE);
                    if (s && *s) {
                        snprintf(buf, size, "%s", (char *)s);
                        result = 1;
                    }
                }
                done = 1;
            } else if (cl == IDCMP_GADGETUP && gad->GadgetID == G_CANCEL)
                done = 1;
        }
    }
    CloseWindow(win);
    FreeGadgets(glist);
    sleep_main(0);
    return result;
}

/* ---- unsaved changes ---- */

#define MAX_TICKS 12

int oe_req_unsaved(int *ticked)
{
    struct Gadget *glist = NULL, *g;
    struct NewGadget ng;
    struct Window *win;
    char labels[MAX_TICKS][80], q[64];
    int which[MAX_TICKS], n = 0, i, fh = A.fh, bh = fh + 6, pad = 12, w = 400, h, top, rowh = fh + 6, result = 0, done = 0;
    int b1 = tw("Cancel") + 24, b2 = tw("Don't save") + 24, b3 = tw("Save ticked") + 24, y;
    for (i = 0; i < A.ntabs && n < MAX_TICKS; i++)
        if (oe_doc_modified(&A.tabs[i]->d)) {
            oe_doc *d = &A.tabs[i]->d;
            const char *nm = d->path[0] ? d->path : d->name;
            size_t l = strlen(nm);
            if (l > sizeof labels[n] - 1) {
                /* Too long to show whole: keep the end, where the name is. */
                nm += l - (sizeof labels[n] - 4);
                strcpy(labels[n], "...");
                strcat(labels[n], nm);
            } else
                strcpy(labels[n], nm);
            which[n++] = i;
            ticked[i] = 1;
        }
    if (!n)
        return 1;
    sprintf(q, n == 1 ? "1 file has changes that aren't saved" : "%d files have changes that aren't saved", n);
    for (i = 0; i < n; i++)
        if (tw(labels[i]) + fh + 40 > w)
            w = tw(labels[i]) + fh + 40;
    if (w > A.scr->Width - 20)
        w = A.scr->Width - 20;
    top = A.scr->WBorTop + A.scr->Font->ta_YSize + 1 + pad;
    h = top + fh + 10 + n * rowh + 10 + bh + pad;
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &A.ta;
    ng.ng_VisualInfo = A.vi;
    g = CreateContext(&glist);
    y = top + fh + 10;
    for (i = 0; i < n; i++, y += rowh)
        g = mk(CHECKBOX_KIND, g, &ng, G_TICK0 + i, labels[i], pad, y + 2, fh + 2, fh + 2, PLACETEXT_RIGHT,
               GTCB_Checked, TRUE, GTCB_Scaled, TRUE, TAG_DONE);
    y += 10;
    g = mk(BUTTON_KIND, g, &ng, G_CANCEL, "Cancel", pad, y, b1, bh, 0, TAG_DONE);
    g = mk(BUTTON_KIND, g, &ng, G_NOSAVE, "Don't save", w - pad - b3 - 8 - b2, y, b2, bh, 0, TAG_DONE);
    g = mk(BUTTON_KIND, g, &ng, G_SAVE, "Save ticked", w - pad - b3, y, b3, bh, 0, TAG_DONE);
    if (!g || !(win = open_req("OpenEdit", w, h, glist, BUTTONIDCMP | CHECKBOXIDCMP))) {
        FreeGadgets(glist);
        return 0;
    }
    sleep_main(1);
    ogt_bold(win->RPort, 1);
    ogt_text(win->RPort, ogt_pen(&A.ctx, "text"), pad, top, q, w - 2 * pad);
    ogt_bold(win->RPort, 0);
    while (!done) {
        struct IntuiMessage *m;
        WaitPort(win->UserPort);
        while ((m = GT_GetIMsg(win->UserPort))) {
            ULONG cl = m->Class;
            UWORD code = m->Code;
            struct Gadget *gad = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            if (cl == IDCMP_REFRESHWINDOW) {
                GT_BeginRefresh(win);
                GT_EndRefresh(win, TRUE);
            } else if (cl == IDCMP_CLOSEWINDOW || (cl == IDCMP_VANILLAKEY && code == 27))
                done = 1;
            else if (cl == IDCMP_GADGETUP) {
                int id = gad->GadgetID;
                if (id >= G_TICK0 && id < G_TICK0 + n)
                    ticked[which[id - G_TICK0]] = (gad->Flags & GFLG_SELECTED) != 0;
                else if (id == G_CANCEL)
                    done = 1;
                else if (id == G_NOSAVE)
                    result = 1, done = 1;
                else if (id == G_SAVE)
                    result = 2, done = 1;
            }
        }
    }
    CloseWindow(win);
    FreeGadgets(glist);
    sleep_main(0);
    return result;
}

/* ---- settings ---- */

static const char *const class_names[OE_C_COUNT] = {
    "Text", "Commands", "Keywords (IF, DO, END)", "Strings", "Variables", "Numbers", "Labels", "Comments"
};
static const char *const scheme_names[] = { "Classic", "Open 4", NULL };

static void swatch(struct Window *win, int x, int y, int s, ogt_rgb c)
{
    ogt_box(win->RPort, ogt_pen(&A.ctx, "string.shadow"), x, y, s, s);
    ogt_box(win->RPort, ogt_pen_rgb(&A.ctx, c), x + 1, y + 1, s - 2, s - 2);
}

static int choose_font(oe_prefs *p)
{
    struct FontRequester *fo;
    int ok = 0;
    if (!AslBase && !(AslBase = OpenLibrary((CONST_STRPTR)"asl.library", 38)))
        return 0;
    fo = AllocAslRequestTags(ASL_FontRequest, ASLFO_Window, (ULONG)A.win, ASLFO_TitleText, (ULONG)"Text font",
                             ASLFO_FixedWidthOnly, TRUE, ASLFO_InitialName, (ULONG)(p->font[0] ? p->font : "topaz.font"),
                             ASLFO_InitialSize, p->font_size ? p->font_size : 8, TAG_DONE);
    if (!fo)
        return 0;
    if (AslRequest(fo, NULL)) {
        snprintf(p->font, sizeof p->font, "%s", (char *)fo->fo_Attr.ta_Name);
        p->font_size = fo->fo_Attr.ta_YSize;
        ok = 1;
    }
    FreeAslRequest(fo);
    return ok;
}

int oe_req_settings(void)
{
    struct Gadget *glist = NULL, *g, *g_font, *g_r, *g_gg, *g_b;
    struct NewGadget ng;
    struct Window *win;
    struct List list;
    struct Node nodes[OE_C_COUNT - 1];
    oe_prefs p = A.prefs;
    int fh = A.fh, bh = fh + 6, pad = 12, lw = tw("Colour scheme") + 16, w = 460, h, top, y, x, done = 0, result = 0;
    int mode = A.ctx.mode == OGT_DARK, sel = 0, cb = fh + 2, col2;
    int sw_x, sw_y, sw_s = 3 * bh;
    char fontname[80];
    int i;

    NewList(&list);
    for (i = 0; i < OE_C_COUNT - 1; i++) {
        memset(&nodes[i], 0, sizeof nodes[i]);
        nodes[i].ln_Name = (char *)class_names[i + 1];
        AddTail(&list, &nodes[i]);
    }
    if (p.font[0])
        snprintf(fontname, sizeof fontname, "%s %d", p.font, p.font_size);
    else
        strcpy(fontname, "The system's text font");

    top = A.scr->WBorTop + A.scr->Font->ta_YSize + 1 + pad;
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &A.ta;
    ng.ng_VisualInfo = A.vi;
    g = CreateContext(&glist);
    x = pad + lw;
    y = top;
    g = mk(CYCLE_KIND, g, &ng, G_SCHEME, "Colour scheme", x, y, 160, bh, PLACETEXT_LEFT,
           GTCY_Labels, (ULONG)scheme_names, GTCY_Active, p.scheme, TAG_DONE);
    y += bh + 6;
    g = g_font = mk(TEXT_KIND, g, &ng, G_FONT, "Font", x, y, w - x - pad - tw("Choose...") - 30, bh, PLACETEXT_LEFT,
                    GTTX_Text, (ULONG)fontname, GTTX_CopyText, TRUE, GTTX_Border, TRUE, TAG_DONE);
    g = mk(BUTTON_KIND, g, &ng, G_FONTBTN, "Choose...", w - pad - tw("Choose...") - 24, y, tw("Choose...") + 24, bh, 0,
           TAG_DONE);
    y += bh + 6;
    g = mk(INTEGER_KIND, g, &ng, G_TABS, "Tab width", x, y, 50, bh, PLACETEXT_LEFT, GTIN_Number, p.tabw,
           GTIN_MaxChars, 2, TAG_DONE);
    g = mk(CHECKBOX_KIND, g, &ng, G_SPACES, "Insert spaces", x + 64, y + (bh - cb) / 2, cb, cb, PLACETEXT_RIGHT,
           GTCB_Checked, p.spaces, GTCB_Scaled, TRUE, TAG_DONE);
    y += bh + 6;
    col2 = x + 160;
    g = mk(CHECKBOX_KIND, g, &ng, G_NUMBERS, "Line numbers", x, y + (bh - cb) / 2, cb, cb, PLACETEXT_RIGHT,
           GTCB_Checked, p.numbers, GTCB_Scaled, TRUE, TAG_DONE);
    g = mk(CHECKBOX_KIND, g, &ng, G_CURLINE, "Current line", col2, y + (bh - cb) / 2, cb, cb, PLACETEXT_RIGHT,
           GTCB_Checked, p.curline, GTCB_Scaled, TRUE, TAG_DONE);
    y += bh + 4;
    g = mk(CHECKBOX_KIND, g, &ng, G_INDENT, "Auto-indent", x, y + (bh - cb) / 2, cb, cb, PLACETEXT_RIGHT,
           GTCB_Checked, p.autoindent, GTCB_Scaled, TRUE, TAG_DONE);
    g = mk(CHECKBOX_KIND, g, &ng, G_COLOURS, "Colours", col2, y + (bh - cb) / 2, cb, cb, PLACETEXT_RIGHT,
           GTCB_Checked, p.colours, GTCB_Scaled, TRUE, TAG_DONE);
    y += bh + 10;
    g = mk(LISTVIEW_KIND, g, &ng, G_LIST, NULL, pad, y, 220, 7 * (fh + 2) + 6, 0, GTLV_Labels, (ULONG)&list,
           GTLV_Selected, sel, GTLV_ShowSelected, 0, TAG_DONE);
    sw_x = pad + 230;
    sw_y = y;
    g = g_r = mk(SLIDER_KIND, g, &ng, G_R, "R", sw_x + sw_s + 24, y, w - sw_x - sw_s - 24 - pad - 30, bh, PLACETEXT_LEFT,
                 GTSL_Min, 0, GTSL_Max, 255, GTSL_Level, p.pen[mode][1].r, GTSL_LevelFormat, (ULONG)"%3ld",
                 GTSL_MaxLevelLen, 3, GTSL_LevelPlace, PLACETEXT_RIGHT, GA_RelVerify, TRUE, TAG_DONE);
    g = g_gg = mk(SLIDER_KIND, g, &ng, G_G, "G", sw_x + sw_s + 24, y + bh + 4, w - sw_x - sw_s - 24 - pad - 30, bh,
                  PLACETEXT_LEFT, GTSL_Min, 0, GTSL_Max, 255, GTSL_Level, p.pen[mode][1].g, GTSL_LevelFormat,
                  (ULONG)"%3ld", GTSL_MaxLevelLen, 3, GTSL_LevelPlace, PLACETEXT_RIGHT, GA_RelVerify, TRUE, TAG_DONE);
    g = g_b = mk(SLIDER_KIND, g, &ng, G_B, "B", sw_x + sw_s + 24, y + 2 * (bh + 4), w - sw_x - sw_s - 24 - pad - 30, bh,
                 PLACETEXT_LEFT, GTSL_Min, 0, GTSL_Max, 255, GTSL_Level, p.pen[mode][1].b, GTSL_LevelFormat,
                 (ULONG)"%3ld", GTSL_MaxLevelLen, 3, GTSL_LevelPlace, PLACETEXT_RIGHT, GA_RelVerify, TRUE, TAG_DONE);
    y += 7 * (fh + 2) + 6 + 12;
    g = mk(BUTTON_KIND, g, &ng, G_CANCEL, "Cancel", pad, y, tw("Cancel") + 24, bh, 0, TAG_DONE);
    g = mk(BUTTON_KIND, g, &ng, G_USE, "Use", w - pad - (tw("Save") + 30) - 8 - (tw("Use") + 30), y, tw("Use") + 30, bh,
           0, TAG_DONE);
    g = mk(BUTTON_KIND, g, &ng, G_SAVE, "Save", w - pad - (tw("Save") + 30), y, tw("Save") + 30, bh, 0, TAG_DONE);
    h = y + bh + pad;
    if (!g || !(win = open_req("OpenEdit settings", w, h, glist,
                               BUTTONIDCMP | CHECKBOXIDCMP | CYCLEIDCMP | LISTVIEWIDCMP | SLIDERIDCMP | INTEGERIDCMP))) {
        FreeGadgets(glist);
        return 0;
    }
    sleep_main(1);
    swatch(win, sw_x, sw_y, sw_s, p.pen[mode][1]);
    while (!done) {
        struct IntuiMessage *m;
        WaitPort(win->UserPort);
        while ((m = GT_GetIMsg(win->UserPort))) {
            ULONG cl = m->Class;
            UWORD code = m->Code;
            struct Gadget *gad = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            if (cl == IDCMP_REFRESHWINDOW) {
                GT_BeginRefresh(win);
                GT_EndRefresh(win, TRUE);
            } else if (cl == IDCMP_CLOSEWINDOW || (cl == IDCMP_VANILLAKEY && code == 27))
                done = 1;
            else if (cl == IDCMP_GADGETUP || cl == IDCMP_GADGETDOWN || cl == IDCMP_MOUSEMOVE) {
                int id = gad->GadgetID, c = sel + 1;
                switch (id) {
                case G_SCHEME:
                    oe_prefs_scheme(&p, code);
                    break;
                case G_FONTBTN:
                    if (choose_font(&p)) {
                        snprintf(fontname, sizeof fontname, "%s %d", p.font, p.font_size);
                        GT_SetGadgetAttrs(g_font, win, NULL, GTTX_Text, (ULONG)fontname, TAG_DONE);
                    }
                    break;
                case G_SPACES:
                    p.spaces = (gad->Flags & GFLG_SELECTED) != 0;
                    break;
                case G_NUMBERS:
                    p.numbers = (gad->Flags & GFLG_SELECTED) != 0;
                    break;
                case G_CURLINE:
                    p.curline = (gad->Flags & GFLG_SELECTED) != 0;
                    break;
                case G_INDENT:
                    p.autoindent = (gad->Flags & GFLG_SELECTED) != 0;
                    break;
                case G_COLOURS:
                    p.colours = (gad->Flags & GFLG_SELECTED) != 0;
                    break;
                case G_LIST:
                    sel = code;
                    c = sel + 1;
                    GT_SetGadgetAttrs(g_r, win, NULL, GTSL_Level, p.pen[mode][c].r, TAG_DONE);
                    GT_SetGadgetAttrs(g_gg, win, NULL, GTSL_Level, p.pen[mode][c].g, TAG_DONE);
                    GT_SetGadgetAttrs(g_b, win, NULL, GTSL_Level, p.pen[mode][c].b, TAG_DONE);
                    break;
                case G_R:
                    p.pen[mode][c].r = (unsigned char)code;
                    break;
                case G_G:
                    p.pen[mode][c].g = (unsigned char)code;
                    break;
                case G_B:
                    p.pen[mode][c].b = (unsigned char)code;
                    break;
                case G_CANCEL:
                    done = 1;
                    break;
                case G_USE:
                case G_SAVE: {
                    struct Gadget *t;
                    /* The tab width is read when leaving, in case Return wasn't pressed. */
                    for (t = glist; t; t = t->NextGadget)
                        if (t->GadgetID == G_TABS) {
                            LONG v = 8;
                            GT_GetGadgetAttrs(t, win, NULL, GTIN_Number, (ULONG)&v, TAG_DONE);
                            p.tabw = v < 1 ? 1 : v > 16 ? 16 : (int)v;
                        }
                    A.prefs = p;
                    oe_prefs_save(&p, id == G_SAVE);
                    result = 1;
                    done = 1;
                    break;
                }
                }
                if (id == G_SCHEME || id == G_LIST || ((id == G_R || id == G_G || id == G_B) && cl == IDCMP_GADGETUP)) {
                    if (id == G_SCHEME) {
                        GT_SetGadgetAttrs(g_r, win, NULL, GTSL_Level, p.pen[mode][c].r, TAG_DONE);
                        GT_SetGadgetAttrs(g_gg, win, NULL, GTSL_Level, p.pen[mode][c].g, TAG_DONE);
                        GT_SetGadgetAttrs(g_b, win, NULL, GTSL_Level, p.pen[mode][c].b, TAG_DONE);
                    }
                    swatch(win, sw_x, sw_y, sw_s, p.pen[mode][c]);
                }
            }
        }
    }
    CloseWindow(win);
    FreeGadgets(glist);
    sleep_main(0);
    return result;
}
