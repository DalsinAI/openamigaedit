/* OpenEdit: the plain text editor for OpenUp (DESIGN.md). One window with a
 * tab per file, find and replace in a bar under the text, colours for
 * scripts and source, and safe saving. GadTools only: nothing to install
 * beyond AmigaOS 3.2, with OpenLook's look drawn by OpenGadTools' code.
 *
 *   OpenEdit [FILES ...] [LINE n] [WAIT] [READONLY] [NEW] [PUBSCREEN name]
 *
 * OpenEdit runs once. Started from a Shell it hands its files to the
 * running copy (starting one in the background first if need be) and
 * returns at once; with WAIT it returns when those tabs close, so tools that
 * call $EDITOR keep working.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */

static const char version[] = "$VER: OpenEdit " "0.1" " (6.10.2026) MIT, Copyright (c) 2026 Dalsin Limited";

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <graphics/text.h>
#include <graphics/gfxbase.h>
#include <devices/inputevent.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/diskfont.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oe_app.h"
#include "oe_find.h"
#include "oe_pat.h"
#include "oe_stack.h"
#include "ogt_font.h"

extern struct GfxBase *GfxBase;
extern struct WBStartup *_WBenchMsg;
struct Library *DiskfontBase;

oe_app A;

#define THEME_DIR "SYS:Prefs/Presets/Themes/"

/* The theme when none is set or found: OpenLook's Open, light. */
static const char fallback_theme[] =
    "name Open\nversion 1\nfont \"DejaVu Sans\" 12\n[light]\n"
    "window #e8eaee\ntext #121825\nlabel #2a3342\nmuted #5d6676\n"
    "button #ffffff 0, #d5dae2 100\nbutton.text #121825\nbutton.border #8a94a6\nbutton.highlight #ffffff\n"
    "string #ffffff\nstring.shadow #8a94a6\nstring.shine #c9cfd9\n"
    "accent #365fa3\naccent.text #ffffff\nfill #a4bde6 0, #7d9fd5 100\nfill.text #121825\n"
    "selection.inactive #cfdaee\nlist #ffffff\nlist.alternate #f1f4f8\nlist.header #f7f8fa 0, #dde2e9 100\n"
    "group.line #a3abb8\ngroup.highlight #ffffff\ntab #e3e7ed 0, #cfd5de 100\ntab.text #2a3342\n"
    "track #cdd3dc\nframe.active #3d4a63\n";

static char title_buf[OE_PATH_MAX + 32];

/* ---- small helpers ---- */

oe_tab *cur_tab(void)
{
    return A.ntabs ? A.tabs[A.cur] : NULL;
}

oe_doc *cur_doc(void)
{
    return A.ntabs ? &A.tabs[A.cur]->d : NULL;
}

void oe_note(const char *title, const char *fmt, ...)
{
    char text[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    oe_req_three(title, text, "OK");
}

static int find_flags(void)
{
    return (A.f_case ? OE_F_CASE : 0) | (A.f_word ? OE_F_WORD : 0) | (A.f_pat ? OE_F_PAT : 0);
}

void oe_set_title(void)
{
    oe_doc *d = cur_doc();
    if (!A.win)
        return;
    snprintf(title_buf, sizeof title_buf, "OpenEdit \xb7 %s", d ? (d->path[0] ? d->path : d->name) : "");
    SetWindowTitles(A.win, (UBYTE *)title_buf, (UBYTE *)~0);
}

static long now_secs(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return ds.ds_Days * 86400L + ds.ds_Minute * 60L + ds.ds_Tick / TICKS_PER_SECOND;
}

/* ---- the theme ---- */

static int load_theme(int *mode)
{
    char line[96], name[96] = "Open", path[200], err[96];
    char *text = NULL, *nl;
    long n;
    *mode = OGT_LIGHT;
    if (oe_read_small("ENV:OpenGadTools/Theme", line, sizeof line)) {
        if ((nl = strchr(line, '\n'))) {
            *nl = 0;
            if (strncmp(nl + 1, "dark", 4) == 0)
                *mode = OGT_DARK;
        }
        if (line[0])
            snprintf(name, sizeof name, "%s", line);
    }
    if (strchr(name, ':'))
        snprintf(path, sizeof path, "%s", name);
    else
        snprintf(path, sizeof path, "ENVARC:OpenGadTools/Themes/%s%s", name, strstr(name, ".theme") ? "" : ".theme");
    text = oe_load_text(path, 65536, &n);
    if (!text) {
        snprintf(path, sizeof path, "%s%s%s", THEME_DIR, name, strstr(name, ".theme") ? "" : ".theme");
        text = oe_load_text(path, 65536, &n);
    }
    if (text) {
        int ok = ogt_theme_parse(&A.theme, text, err, sizeof err);
        free(text);
        if (ok)
            return 1;
    }
    *mode = OGT_LIGHT;
    return ogt_theme_parse(&A.theme, fallback_theme, err, sizeof err);
}

/* The text font: the one in Settings, else the system's own text font. */
static int open_text_font(void)
{
    struct TextAttr ta;
    struct TextFont *f = NULL;
    if (A.tx_font)
        CloseFont(A.tx_font);
    A.tx_font = NULL;
    if (A.prefs.font[0]) {
        ta.ta_Name = (STRPTR)A.prefs.font;
        ta.ta_YSize = A.prefs.font_size ? A.prefs.font_size : 8;
        ta.ta_Style = 0;
        ta.ta_Flags = 0;
        if (!DiskfontBase)
            DiskfontBase = OpenLibrary((CONST_STRPTR)"diskfont.library", 36);
        f = DiskfontBase ? OpenDiskFont(&ta) : OpenFont(&ta);
    }
    if (!f) {
        struct TextFont *def = GfxBase->DefaultFont;
        ta.ta_Name = (STRPTR)def->tf_Message.mn_Node.ln_Name;
        ta.ta_YSize = def->tf_YSize;
        ta.ta_Style = 0;
        ta.ta_Flags = 0;
        f = OpenFont(&ta);
    }
    if (!f)
        return 0;
    A.tx_font = f;
    A.cw = f->tf_XSize > 0 ? f->tf_XSize : 8;
    A.lh = f->tf_YSize + 1;
    return 1;
}

/* ---- menus ---- */

static struct NewMenu base_menu[] = {
    { NM_TITLE, (STRPTR)"Project", 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"New", (STRPTR)"N", 0, 0, (APTR)C_NEW },
    { NM_ITEM, (STRPTR)"Open...", (STRPTR)"O", 0, 0, (APTR)C_OPEN },
    { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"Save", (STRPTR)"S", 0, 0, (APTR)C_SAVE },
    { NM_ITEM, (STRPTR)"Save as...", 0, 0, 0, (APTR)C_SAVEAS },
    { NM_ITEM, (STRPTR)"Save all", 0, 0, 0, (APTR)C_SAVEALL },
    { NM_ITEM, (STRPTR)"Revert to saved", 0, 0, 0, (APTR)C_REVERT },
    { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"Insert file...", 0, 0, 0, (APTR)C_INSFILE },
    { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"Close tab", (STRPTR)"K", 0, 0, (APTR)C_CLOSE },
    { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"About...", 0, 0, 0, (APTR)C_ABOUT },
    { NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)C_QUIT },
    { NM_TITLE, (STRPTR)"Edit", 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"Undo", (STRPTR)"Z", 0, 0, (APTR)C_UNDO },
    { NM_ITEM, (STRPTR)"Redo", (STRPTR)"Y", 0, 0, (APTR)C_REDO },
    { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"Cut", (STRPTR)"X", 0, 0, (APTR)C_CUT },
    { NM_ITEM, (STRPTR)"Copy", (STRPTR)"C", 0, 0, (APTR)C_COPY },
    { NM_ITEM, (STRPTR)"Paste", (STRPTR)"V", 0, 0, (APTR)C_PASTE },
    { NM_ITEM, (STRPTR)"Delete", 0, 0, 0, (APTR)C_DELETE },
    { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"Select all", (STRPTR)"A", 0, 0, (APTR)C_SELALL },
    { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"Indent", (STRPTR)"]", 0, 0, (APTR)C_INDENT },
    { NM_ITEM, (STRPTR)"Outdent", (STRPTR)"[", 0, 0, (APTR)C_OUTDENT },
    { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"Overwrite", 0, CHECKIT | MENUTOGGLE, 0, (APTR)C_OVERWRITE },
    { NM_TITLE, (STRPTR)"Search", 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"Find...", (STRPTR)"F", 0, 0, (APTR)C_FIND },
    { NM_ITEM, (STRPTR)"Find next", (STRPTR)"G", 0, 0, (APTR)C_FINDNEXT },
    { NM_ITEM, (STRPTR)"Find previous", (STRPTR)"P", 0, 0, (APTR)C_FINDPREV },
    { NM_ITEM, (STRPTR)"Replace...", (STRPTR)"R", 0, 0, (APTR)C_REPLACE },
    { NM_ITEM, (STRPTR)"Replace all", 0, 0, 0, (APTR)C_REPLACEALL },
    { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"Go to line...", (STRPTR)"L", 0, 0, (APTR)C_GOTO },
    { NM_TITLE, (STRPTR)"View", 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"Line numbers", 0, CHECKIT | MENUTOGGLE, 0, (APTR)C_NUMBERS },
    { NM_ITEM, (STRPTR)"Current line", 0, CHECKIT | MENUTOGGLE, 0, (APTR)C_CURLINE },
    { NM_ITEM, (STRPTR)"Colours", 0, CHECKIT | MENUTOGGLE, 0, (APTR)C_COLOURS },
    { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"Next tab", 0, 0, 0, (APTR)C_NEXTTAB },
    { NM_ITEM, (STRPTR)"Previous tab", 0, 0, 0, (APTR)C_PREVTAB },
    { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"Colouring", 0, 0, 0, 0 },
    /* the kinds go here */
};
static struct NewMenu tail_menu[] = {
    { NM_TITLE, (STRPTR)"Tools", 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"Character set", 0, 0, 0, 0 },
    { NM_SUB, (STRPTR)"ISO-8859-1", 0, CHECKIT, 0, (APTR)C_LATIN1 },
    { NM_SUB, (STRPTR)"UTF-8", 0, CHECKIT, 0, (APTR)C_UTF8 },
    { NM_ITEM, (STRPTR)"Line ends", 0, 0, 0, 0 },
    { NM_SUB, (STRPTR)"LF (Amiga)", 0, CHECKIT, 0, (APTR)C_LF },
    { NM_SUB, (STRPTR)"CR LF (Windows)", 0, CHECKIT, 0, (APTR)C_CRLF },
    { NM_SUB, (STRPTR)"CR (old Mac)", 0, CHECKIT, 0, (APTR)C_CR },
    { NM_ITEM, (STRPTR)"Read only", 0, CHECKIT | MENUTOGGLE, 0, (APTR)C_READONLY },
    { NM_TITLE, (STRPTR)"Settings", 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"Settings...", 0, 0, 0, (APTR)C_SETTINGS },
    { NM_END, 0, 0, 0, 0, 0 },
};

static struct NewMenu *menu_def;

static int build_menus(void)
{
    int nb = sizeof base_menu / sizeof base_menu[0], nt = sizeof tail_menu / sizeof tail_menu[0], i, k = 0;
    if (!(menu_def = calloc(nb + 1 + A.nkinds + nt, sizeof *menu_def)))
        return 0;
    for (i = 0; i < nb; i++)
        menu_def[k++] = base_menu[i];
    menu_def[k].nm_Type = NM_SUB;
    menu_def[k].nm_Label = (STRPTR)"Plain text";
    menu_def[k].nm_Flags = CHECKIT;
    menu_def[k++].nm_UserData = (APTR)C_PLAIN;
    for (i = 0; i < A.nkinds; i++) {
        menu_def[k].nm_Type = NM_SUB;
        menu_def[k].nm_Label = (STRPTR)A.kinds[i].name;
        menu_def[k].nm_Flags = CHECKIT;
        menu_def[k++].nm_UserData = (APTR)(LONG)(C_KIND0 + i);
    }
    for (i = 0; i < nt; i++)
        menu_def[k++] = tail_menu[i];
    if (!(A.menus = CreateMenus(menu_def, GTMN_FrontPen, 0, TAG_DONE)))
        return 0;
    return LayoutMenus(A.menus, A.vi, GTMN_NewLookMenus, TRUE, TAG_DONE) != 0;
}

/* Sets the check marks that follow the current tab and the settings. */
static void sync_menus(void)
{
    oe_tab *t = cur_tab();
    oe_doc *d = cur_doc();
    struct Menu *m;
    if (!A.win || !A.menus || !d)
        return;
    ClearMenuStrip(A.win);
    for (m = A.menus; m; m = m->NextMenu) {
        struct MenuItem *it;
        for (it = m->FirstItem; it; it = it->NextItem) {
            struct MenuItem *sub, *list[2];
            int j;
            list[0] = it;
            list[1] = it->SubItem;
            for (j = 0; j < 2; j++)
                for (sub = list[j]; sub; sub = j ? sub->NextItem : NULL) {
                    LONG cmd = (LONG)GTMENUITEM_USERDATA(sub);
                    int on = -1;
                    switch (cmd) {
                    case C_NUMBERS: on = A.prefs.numbers; break;
                    case C_CURLINE: on = A.prefs.curline; break;
                    case C_COLOURS: on = A.prefs.colours; break;
                    case C_OVERWRITE: on = t->overwrite; break;
                    case C_READONLY: on = d->readonly; break;
                    case C_LATIN1: on = d->cs == OE_CS_LATIN1; break;
                    case C_UTF8: on = d->cs == OE_CS_UTF8; break;
                    case C_LF: on = d->eol == OE_EOL_LF; break;
                    case C_CRLF: on = d->eol == OE_EOL_CRLF; break;
                    case C_CR: on = d->eol == OE_EOL_CR; break;
                    case C_PLAIN: on = d->kind < 0; break;
                    default:
                        if (cmd >= C_KIND0 && cmd < C_KIND0 + A.nkinds)
                            on = d->kind == cmd - C_KIND0;
                    }
                    if (on >= 0) {
                        if (on)
                            sub->Flags |= CHECKED;
                        else
                            sub->Flags &= ~CHECKED;
                    }
                }
        }
    }
    ResetMenuStrip(A.win, A.menus);
}

/* ---- tabs ---- */

static void reply_waiter(oe_tab *t, LONG rc)
{
    if (t->waiter) {
        t->waiter->rc = rc;
        ReplyMsg(&t->waiter->m);
        t->waiter = NULL;
    }
}

static int is_blank_untitled(oe_tab *t)
{
    return !t->d.path[0] && !oe_doc_modified(&t->d) && oe_doc_len(&t->d) == 0 && !t->waiter;
}

static void free_tab(oe_tab *t)
{
    reply_waiter(t, 0);
    oe_doc_free(&t->d);
    free(t);
}

static void refresh_all(void)
{
    oe_set_title();
    sync_menus();
    oe_layout();
    oe_draw_all();
}

int oe_new_tab(const char *path, long line, int flags, oe_openmsg *waiter)
{
    oe_tab *t;
    int i;
    if (path && path[0]) {
        for (i = 0; i < A.ntabs; i++)
            if (!oe_stricmp(A.tabs[i]->d.path, path)) {
                oe_tab *o = A.tabs[i];
                if (waiter) {
                    if (o->waiter)
                        reply_waiter(o, 0);
                    o->waiter = waiter;
                }
                if (line > 0)
                    oe_doc_set_caret(&o->d, oe_doc_pos_at(&o->d, line - 1, 0), 0);
                oe_switch_tab(i);
                return 1;
            }
    }
    if (A.ntabs >= MAX_DOCS) {
        oe_note("OpenEdit", "%d files are open, which is as many as OpenEdit holds.\nClose one and try again.", MAX_DOCS);
        return 0;
    }
    if (!(t = calloc(1, sizeof *t)))
        return 0;
    if (!oe_doc_init(&t->d)) {
        free(t);
        return 0;
    }
    t->d.tabw = A.prefs.tabw;
    t->d.spaces = A.prefs.spaces;
    t->d.autoindent = A.prefs.autoindent;
    if (path && path[0]) {
        if (!oe_load_file(&t->d, path)) {
            LONG err = IoErr();
            if (err == ERROR_OBJECT_NOT_FOUND) {
                /* A new file: it is made when saved. */
                snprintf(t->d.path, sizeof t->d.path, "%s", path);
                oe_pick_kind(&t->d);
            } else {
                char why[80];
                Fault(err, NULL, (STRPTR)why, sizeof why);
                oe_note("OpenEdit", "Couldn't open %s:\n%s", path, err ? why : "not enough memory");
                oe_doc_free(&t->d);
                free(t);
                return 0;
            }
        }
    } else
        snprintf(t->d.name, sizeof t->d.name, "Untitled %d", ++A.untitled);
    t->d.readonly = (flags & OE_OPEN_READONLY) != 0;
    if (line > 0)
        oe_doc_set_caret(&t->d, oe_doc_pos_at(&t->d, line - 1, 0), 0);
    t->waiter = waiter;
    /* An empty Untitled tab nobody typed in gives way to the file. */
    if (path && path[0] && A.ntabs == 1 && is_blank_untitled(A.tabs[0])) {
        free_tab(A.tabs[0]);
        A.ntabs = 0;
        A.untitled = 0;
    }
    A.tabs[A.ntabs] = t;
    A.cur = A.ntabs++;
    if (A.win)
        refresh_all();
    return 1;
}

void oe_switch_tab(int i)
{
    if (i < 0 || i >= A.ntabs)
        return;
    if (A.cur < A.ntabs)
        oe_doc_break_typing(cur_doc());
    A.cur = i;
    if (A.win) {
        refresh_all();
        WindowToFront(A.win);
    }
}

static int save_tab(oe_tab *t, int ask_name)
{
    char path[OE_PATH_MAX], err[160];
    oe_doc *d = &t->d;
    snprintf(path, sizeof path, "%s", d->path);
    if (ask_name || !path[0]) {
        if (!oe_ask_file(1, "Save as", path, sizeof path))
            return 0;
        if (oe_stricmp(path, d->path)) {
            BPTR l = Lock((CONST_STRPTR)path, SHARED_LOCK);
            if (l) {
                char q[OE_PATH_MAX + 40];
                UnLock(l);
                snprintf(q, sizeof q, "%s is already there.\nReplace it?", path);
                if (oe_req_three("Save as", q, "Replace|Cancel") != 1)
                    return 0;
            }
        }
    }
    if (d->readonly) {
        oe_note("Save", "This file is open read only.\nSwitch off Tools \xbb Read only to save it.");
        return 0;
    }
    if (!oe_save_file(d, path, err, sizeof err)) {
        oe_note("Couldn't save", "%s\n\n%s", path, err);
        return 0;
    }
    t->saved_secs = now_secs();
    return 1;
}

int oe_close_tab(int i, int ask)
{
    oe_tab *t;
    if (i < 0 || i >= A.ntabs)
        return 0;
    t = A.tabs[i];
    if (ask && oe_doc_modified(&t->d)) {
        char q[OE_PATH_MAX + 64];
        int r;
        snprintf(q, sizeof q, "Save the changes to %s?", t->d.path[0] ? t->d.path : t->d.name);
        A.cur = i;
        r = oe_req_three("Close tab", q, "Save|Don't save|Cancel");
        if (r == 0)
            return 0;
        if (r == 1 && !save_tab(t, 0))
            return 0;
    }
    free_tab(t);
    memmove(A.tabs + i, A.tabs + i + 1, (A.ntabs - i - 1) * sizeof A.tabs[0]);
    A.ntabs--;
    if (!A.ntabs) {
        A.quit = 1;
        return 1;
    }
    if (A.cur >= A.ntabs)
        A.cur = A.ntabs - 1;
    else if (A.cur > i)
        A.cur--;
    if (A.first_tab >= A.ntabs)
        A.first_tab = 0;
    refresh_all();
    return 1;
}

/* Quitting: one question for every changed tab. 1 when it may go. */
static int may_quit(void)
{
    int ticked[MAX_DOCS], i, r;
    memset(ticked, 0, sizeof ticked);
    for (i = 0; i < A.ntabs; i++)
        if (oe_doc_modified(&A.tabs[i]->d))
            break;
    if (i == A.ntabs)
        return 1;
    r = oe_req_unsaved(ticked);
    if (r == 0)
        return 0;
    if (r == 2)
        for (i = 0; i < A.ntabs; i++)
            if (ticked[i] && oe_doc_modified(&A.tabs[i]->d)) {
                A.cur = i;
                oe_set_title();
                if (!save_tab(A.tabs[i], 0)) {
                    refresh_all();
                    return 0;
                }
            }
    return 1;
}

/* ---- refreshing after a change ---- */

typedef struct snap {
    long lines, top, caret, anchor, edits;
    int left, modified, digits;
} snap;

static int ndigits(long n)
{
    int d = 1;
    while (n >= 10) {
        n /= 10;
        d++;
    }
    return d < 3 ? 3 : d;
}

static void take(snap *s)
{
    oe_doc *d = cur_doc();
    if (!d) {                   /* no tab open: refresh() draws nothing then */
        memset(s, 0, sizeof *s);
        return;
    }
    s->lines = oe_doc_lines(d);
    s->top = d->top;
    s->caret = d->caret;
    s->anchor = d->anchor;
    s->edits = d->edits;
    s->left = d->left;
    s->modified = oe_doc_modified(d);
    s->digits = ndigits(s->lines);
}

static long minl(long a, long b)
{
    return a < b ? a : b;
}

static long maxl(long a, long b)
{
    return a > b ? a : b;
}

/* Draws what a change touched: a line, the rest of the view, or all. */
static void refresh(const snap *s)
{
    oe_doc *d = cur_doc();
    long lines, cl, l0, l1, ocl, oal, nal, newtop;
    int edited;
    if (!d)
        return;
    lines = oe_doc_lines(d);
    edited = d->edits != s->edits;
    oe_show_caret();
    if (A.prefs.numbers && ndigits(lines) != s->digits) {
        oe_layout();
        oe_draw_all();
        return;
    }
    cl = oe_doc_line(d);
    ocl = oe_buf_line_of(&d->buf, s->caret < oe_doc_len(d) ? s->caret : oe_doc_len(d));
    oal = oe_buf_line_of(&d->buf, s->anchor < oe_doc_len(d) ? s->anchor : oe_doc_len(d));
    nal = oe_buf_line_of(&d->buf, d->anchor);
    l0 = minl(minl(ocl, oal), minl(cl, nal));
    l1 = maxl(maxl(ocl, oal), maxl(cl, nal));
    if (d->left != s->left || (edited && d->top != s->top)) {
        oe_draw_text();
        oe_update_scroller();
    } else if (d->top != s->top) {
        newtop = d->top;
        d->top = s->top;
        oe_scroll_to(newtop);
        oe_draw_lines(l0, l1);
    } else if (edited && lines != s->lines) {
        oe_draw_lines(l0, d->top + A.rows - 1);
        oe_update_scroller();
    } else {
        oe_draw_lines(l0, l1);
        if (edited)
            oe_draw_restate(l1);
    }
    if (oe_doc_modified(d) != s->modified)
        oe_draw_tabs();
    oe_draw_status();
    if (A.find_on && edited)
        oe_count_hits();
}

void oe_after_edit(long old_lines, long old_top, int old_left, long old_caret_line)
{
    (void)old_lines;
    (void)old_top;
    (void)old_left;
    (void)old_caret_line;
    oe_layout();
    oe_draw_all();
}

/* ---- find ---- */

static void read_find_fields(void)
{
    STRPTR s = NULL;
    if (A.g_find && GT_GetGadgetAttrs(A.g_find, A.win, NULL, GTST_String, (ULONG)&s, TAG_DONE) && s)
        snprintf(A.find_text, sizeof A.find_text, "%s", (char *)s);
    s = NULL;
    if (A.g_repl && GT_GetGadgetAttrs(A.g_repl, A.win, NULL, GTST_String, (ULONG)&s, TAG_DONE) && s)
        snprintf(A.repl_text, sizeof A.repl_text, "%s", (char *)s);
}

int oe_find_next(int backward, int note)
{
    oe_doc *d = cur_doc();
    long a, b, sa, sb;
    snap s;
    if (!d || !A.find_text[0])
        return 0;
    take(&s);
    oe_doc_sel(d, &sa, &sb);
    if (!oe_find(d, A.find_text, find_flags(), backward ? sa : (sb > sa ? sb : d->caret), backward, &a, &b)) {
        if (note)
            DisplayBeep(A.scr);
        if (A.find_on)
            oe_count_hits();
        return 0;
    }
    oe_doc_select(d, a, b);
    refresh(&s);
    if (A.find_on) {
        /* All the matches in view are marked: draw them all. */
        oe_draw_text();
        oe_count_hits();
    }
    return 1;
}

static void show_find(int replace)
{
    oe_doc *d = cur_doc();
    long a, b;
    oe_doc_sel(d, &a, &b);
    if (b > a && b - a < (long)sizeof A.find_text - 1 && oe_buf_line_of(&d->buf, a) == oe_buf_line_of(&d->buf, b)) {
        long n;
        char *t = oe_doc_text(d, a, b, &n);
        if (t) {
            snprintf(A.find_text, sizeof A.find_text, "%s", t);
            free(t);
        }
    }
    if (!A.find_on) {
        A.find_on = 1;
        oe_layout();
        oe_draw_all();
    } else {
        read_find_fields();
        GT_SetGadgetAttrs(A.g_find, A.win, NULL, GTST_String, (ULONG)A.find_text, TAG_DONE);
        oe_count_hits();
    }
    if (replace ? A.g_repl : A.g_find)
        ActivateGadget(replace ? A.g_repl : A.g_find, A.win, NULL);
}

static void hide_find(void)
{
    if (!A.find_on)
        return;
    read_find_fields();
    A.find_on = 0;
    oe_layout();
    oe_draw_all();
}

static void replace_one(void)
{
    oe_doc *d = cur_doc();
    long a, b, ma, mb;
    snap s;
    read_find_fields();
    if (!A.find_text[0])
        return;
    oe_doc_sel(d, &a, &b);
    take(&s);
    if (b > a && oe_find(d, A.find_text, find_flags(), a, 0, &ma, &mb) && ma == a && mb == b) {
        oe_doc_replace(d, a, b, A.repl_text, (long)strlen(A.repl_text));
        oe_doc_set_caret(d, a + (long)strlen(A.repl_text), 0);
        refresh(&s);
    }
    oe_find_next(0, 1);
}

static void replace_all(void)
{
    long n = 0;
    int i;
    static char msg[48];            /* GadTools keeps the pointer */
    read_find_fields();
    if (!A.find_text[0])
        return;
    if (A.f_alltabs) {
        for (i = 0; i < A.ntabs; i++)
            n += oe_replace_all(&A.tabs[i]->d, A.find_text, find_flags(), A.repl_text);
    } else
        n = oe_replace_all(cur_doc(), A.find_text, find_flags(), A.repl_text);
    oe_layout();
    oe_draw_all();
    sprintf(msg, "Replaced %ld", n);
    if (A.g_count)
        GT_SetGadgetAttrs(A.g_count, A.win, NULL, GTTX_Text, (ULONG)msg, TAG_DONE);
}

/* ---- character sets ---- */

static void set_charset(oe_doc *d, int cs)
{
    long n, i, o = 0;
    char *t, *u;
    if (!d || d->cs == cs)
        return;
    if (!(t = oe_doc_text(d, 0, oe_doc_len(d), &n)))
        return;
    if (!(u = malloc(n * 2 + 1))) {
        free(t);
        return;
    }
    for (i = 0; i < n; i++) {
        unsigned c = (unsigned char)t[i];
        if (cs == OE_CS_UTF8) {
            if (c < 0x80)
                u[o++] = (char)c;
            else {
                u[o++] = (char)(0xc0 | (c >> 6));
                u[o++] = (char)(0x80 | (c & 0x3f));
            }
        } else {
            if (c < 0x80)
                u[o++] = (char)c;
            else if ((c & 0xe0) == 0xc0 && i + 1 < n) {
                unsigned v = ((c & 0x1f) << 6) | ((unsigned char)t[i + 1] & 0x3f);
                u[o++] = v < 256 ? (char)v : '?';
                i++;
            } else {
                u[o++] = '?';
                while (i + 1 < n && ((unsigned char)t[i + 1] & 0xc0) == 0x80)
                    i++;
            }
        }
    }
    oe_doc_replace(d, 0, n, u, o);
    oe_doc_set_caret(d, 0, 0);
    d->cs = cs;
    if (cs == OE_CS_LATIN1)
        d->bom = 0;
    free(t);
    free(u);
}

/* ---- commands ---- */

static void insert_text(const char *s, long n)
{
    oe_doc *d = cur_doc();
    snap sn;
    if (!d)
        return;
    take(&sn);
    if (d->cs == OE_CS_UTF8) {
        /* The clipboard and the keyboard speak ISO-8859-1. */
        char *u = malloc(n * 2 + 1);
        long i, o = 0;
        if (!u)
            return;
        for (i = 0; i < n; i++) {
            unsigned c = (unsigned char)s[i];
            if (c < 0x80)
                u[o++] = (char)c;
            else {
                u[o++] = (char)(0xc0 | (c >> 6));
                u[o++] = (char)(0x80 | (c & 0x3f));
            }
        }
        oe_doc_insert(d, u, o, 0);
        free(u);
    } else
        oe_doc_insert(d, s, n, 0);
    refresh(&sn);
}

static void copy_sel(int cut)
{
    oe_doc *d = cur_doc();
    long a, b, n, i, o = 0;
    char *t;
    snap s;
    oe_doc_sel(d, &a, &b);
    if (a == b || !(t = oe_doc_text(d, a, b, &n)))
        return;
    if (d->cs == OE_CS_UTF8) {
        /* To ISO-8859-1 for the clipboard. */
        for (i = 0; i < n; i++) {
            unsigned c = (unsigned char)t[i];
            if (c < 0x80)
                t[o++] = (char)c;
            else if ((c & 0xe0) == 0xc0 && i + 1 < n) {
                unsigned v = ((c & 0x1f) << 6) | ((unsigned char)t[i + 1] & 0x3f);
                t[o++] = v < 256 ? (char)v : '?';
                i++;
            } else {
                t[o++] = '?';
                while (i + 1 < n && ((unsigned char)t[i + 1] & 0xc0) == 0x80)
                    i++;
            }
        }
        n = o;
    }
    oe_clip_write(t, n);
    free(t);
    if (cut) {
        take(&s);
        oe_doc_delete_sel(d);
        refresh(&s);
    }
}

static void about(void)
{
    oe_note("About OpenEdit", "OpenEdit " OE_VERSION " (" OE_DATE ")\nThe plain text editor for OpenUp.\n\n"
                              "MIT licence, Copyright \xa9 2026 Dalsin Limited.");
}

void oe_command(int cmd)
{
    oe_tab *t = cur_tab();
    oe_doc *d = cur_doc();
    snap s;
    char path[OE_PATH_MAX];
    if (!d)
        return;
    take(&s);
    switch (cmd) {
    case C_NEW:
        oe_new_tab(NULL, 0, 0, NULL);
        break;
    case C_OPEN:
        snprintf(path, sizeof path, "%s", d->path);
        if (path[0])
            *(char *)FilePart((STRPTR)path) = 0;
        if (oe_ask_file(0, "Open", path, sizeof path))
            oe_new_tab(path, 0, 0, NULL);
        break;
    case C_SAVE:
    case C_SAVEAS:
        if (save_tab(t, cmd == C_SAVEAS))
            refresh_all();
        break;
    case C_SAVEALL: {
        int i, keep = A.cur;
        for (i = 0; i < A.ntabs; i++)
            if (oe_doc_modified(&A.tabs[i]->d)) {
                A.cur = i;
                if (!save_tab(A.tabs[i], 0))
                    break;
            }
        A.cur = keep;
        refresh_all();
        break;
    }
    case C_REVERT:
        if (!d->path[0] || !oe_doc_modified(d))
            break;
        if (oe_req_three("Revert", "Throw away the changes and go back to the saved file?", "Revert|Cancel") == 1) {
            long top = d->top;
            if (oe_load_file(d, d->path))
                d->top = top;
            refresh_all();
        }
        break;
    case C_INSFILE:
        path[0] = 0;
        if (oe_ask_file(0, "Insert file", path, sizeof path)) {
            long n;
            char *txt = oe_load_text(path, 32L * 1024 * 1024, &n);
            if (txt) {
                oe_doc_insert(d, txt, n, 0);
                free(txt);
                oe_layout();
                oe_draw_all();
            }
        }
        break;
    case C_CLOSE:
        oe_close_tab(A.cur, 1);
        break;
    case C_ABOUT:
        about();
        break;
    case C_QUIT:
        if (may_quit())
            A.quit = 1;
        break;
    case C_UNDO:
        oe_doc_undo(d);
        refresh(&s);
        break;
    case C_REDO:
        oe_doc_redo(d);
        refresh(&s);
        break;
    case C_CUT:
        copy_sel(1);
        break;
    case C_COPY:
        copy_sel(0);
        break;
    case C_PASTE: {
        long n;
        char *txt = oe_clip_read(&n);
        if (txt) {
            insert_text(txt, n);
            free(txt);
        }
        break;
    }
    case C_DELETE:
        oe_doc_delete_sel(d);
        refresh(&s);
        break;
    case C_SELALL:
        oe_doc_select_all(d);
        refresh(&s);
        break;
    case C_INDENT:
    case C_OUTDENT:
        oe_doc_tab(d, cmd == C_OUTDENT);
        refresh(&s);
        break;
    case C_OVERWRITE:
        t->overwrite = !t->overwrite;
        sync_menus();
        oe_draw_status();
        break;
    case C_FIND:
        show_find(0);
        break;
    case C_REPLACE:
        show_find(1);
        break;
    case C_FINDNEXT:
    case C_FINDPREV:
        read_find_fields();
        if (!A.find_text[0])
            show_find(0);
        else
            oe_find_next(cmd == C_FINDPREV, 1);
        break;
    case C_REPLACEALL:
        if (!A.find_on)
            show_find(1);
        else
            replace_all();
        break;
    case C_FINDCLOSE:
        hide_find();
        break;
    case C_GOTO: {
        char buf[24];
        sprintf(buf, "%ld", oe_doc_line(d) + 1);
        if (oe_req_string("Go to line", "Line number", "Go", buf, sizeof buf, 1)) {
            long l = atol(buf);
            if (l < 1)
                l = 1;
            oe_doc_set_caret(d, oe_doc_pos_at(d, l - 1, 0), 0);
            /* Put the line a third of the way down. */
            d->top = l - 1 - A.rows / 3;
            if (d->top < 0)
                d->top = 0;
            oe_show_caret();
            oe_draw_text();
            oe_update_scroller();
            oe_draw_status();
        }
        break;
    }
    case C_NUMBERS:
    case C_CURLINE:
    case C_COLOURS:
        if (cmd == C_NUMBERS)
            A.prefs.numbers = !A.prefs.numbers;
        else if (cmd == C_CURLINE)
            A.prefs.curline = !A.prefs.curline;
        else
            A.prefs.colours = !A.prefs.colours;
        oe_prefs_save(&A.prefs, 0);
        refresh_all();
        break;
    case C_NEXTTAB:
        oe_switch_tab((A.cur + 1) % A.ntabs);
        break;
    case C_PREVTAB:
        oe_switch_tab((A.cur + A.ntabs - 1) % A.ntabs);
        break;
    case C_PLAIN:
        d->kind = -1;
        d->lsok = 0;
        refresh_all();
        break;
    case C_LATIN1:
    case C_UTF8:
        set_charset(d, cmd == C_UTF8 ? OE_CS_UTF8 : OE_CS_LATIN1);
        refresh_all();
        break;
    case C_LF:
    case C_CRLF:
    case C_CR:
        if (d->eol != (cmd == C_LF ? OE_EOL_LF : cmd == C_CRLF ? OE_EOL_CRLF : OE_EOL_CR)) {
            d->eol = cmd == C_LF ? OE_EOL_LF : cmd == C_CRLF ? OE_EOL_CRLF : OE_EOL_CR;
            d->saved_at = -1;           /* the file on disk now differs */
        }
        refresh_all();
        break;
    case C_READONLY:
        d->readonly = !d->readonly;
        refresh_all();
        break;
    case C_SETTINGS:
        if (oe_req_settings()) {
            int i;
            open_text_font();
            oe_obtain_pens();
            for (i = 0; i < A.ntabs; i++) {
                A.tabs[i]->d.tabw = A.prefs.tabw;
                A.tabs[i]->d.spaces = A.prefs.spaces;
                A.tabs[i]->d.autoindent = A.prefs.autoindent;
            }
            refresh_all();
        }
        break;
    default:
        if (cmd >= C_KIND0 && cmd < C_KIND0 + A.nkinds) {
            d->kind = cmd - C_KIND0;
            d->lsok = 0;
            refresh_all();
        }
    }
}

/* ---- keys ---- */

#define RAW_UP 0x4c
#define RAW_DOWN 0x4d
#define RAW_RIGHT 0x4e
#define RAW_LEFT 0x4f
#define RAW_DEL 0x46
#define RAW_HELP 0x5f
#define RAW_PGUP 0x48
#define RAW_PGDN 0x49
#define RAW_HOME 0x70
#define RAW_END 0x71
#define RAW_WHEEL_UP 0x7a
#define RAW_WHEEL_DOWN 0x7b

#define Q_SHIFT (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)
#define Q_ALT (IEQUALIFIER_LALT | IEQUALIFIER_RALT)
#define Q_CTRL IEQUALIFIER_CONTROL

static void raw_key(UWORD code, UWORD qual)
{
    oe_doc *d = cur_doc();
    int ext = (qual & Q_SHIFT) != 0, alt = (qual & Q_ALT) != 0, ctrl = (qual & Q_CTRL) != 0, how = 0;
    snap s;
    if (code & IECODE_UP_PREFIX)
        return;
    take(&s);
    switch (code) {
    case RAW_UP:
        how = ctrl ? OE_TOP : alt ? OE_PGUP : OE_UP;
        break;
    case RAW_DOWN:
        how = ctrl ? OE_BOTTOM : alt ? OE_PGDN : OE_DOWN;
        break;
    case RAW_LEFT:
        how = ctrl ? OE_HOME : alt ? OE_WORDL : OE_LEFT;
        break;
    case RAW_RIGHT:
        how = ctrl ? OE_END : alt ? OE_WORDR : OE_RIGHT;
        break;
    case RAW_PGUP:
        how = OE_PGUP;
        break;
    case RAW_PGDN:
        how = OE_PGDN;
        break;
    case RAW_HOME:
        how = ctrl ? OE_TOP : OE_HOME;
        break;
    case RAW_END:
        how = ctrl ? OE_BOTTOM : OE_END;
        break;
    case RAW_DEL:
        if (alt) {
            /* Alt-Del: to the end of the word. */
            long p = d->caret;
            oe_doc_move(d, OE_WORDR, 0, 0);
            oe_doc_select(d, p, d->caret);
        }
        oe_doc_delete(d);
        refresh(&s);
        return;
    case RAW_WHEEL_UP:
    case RAW_WHEEL_DOWN:
        oe_scroll_to(d->top + (code == RAW_WHEEL_UP ? -3 : 3));
        return;
    default:
        return;
    }
    oe_doc_move(d, how, ext, A.rows);
    refresh(&s);
}

static void typed(UBYTE c, UWORD qual)
{
    oe_tab *t = cur_tab();
    oe_doc *d = &t->d;
    snap s;
    /* An Amiga key with no menu shortcut: Intuition passes it on, but it
     * isn't typing. */
    if (qual & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND))
        return;
    take(&s);
    switch (c) {
    case 8:
        if (qual & Q_ALT) {
            long p = d->caret;
            oe_doc_move(d, OE_WORDL, 0, 0);
            oe_doc_select(d, d->caret, p);
        }
        oe_doc_backspace(d);
        break;
    case 127:
        oe_doc_delete(d);
        break;
    case 13:
    case 10:
        oe_doc_newline(d);
        break;
    case 9:
        if (qual & Q_CTRL) {
            oe_command(qual & Q_SHIFT ? C_PREVTAB : C_NEXTTAB);
            return;
        }
        oe_doc_tab(d, (qual & Q_SHIFT) != 0);
        break;
    case 27:
        if (A.find_on)
            hide_find();
        else if (oe_doc_has_sel(d)) {
            oe_doc_set_caret(d, d->caret, 0);
            refresh(&s);
        }
        return;
    default: {
        char u[2];
        int n = 1;
        if (c < 32 || (c >= 0x80 && c < 0xa0))
            return;
        if (d->cs == OE_CS_UTF8 && c >= 0x80) {
            u[0] = (char)(0xc0 | (c >> 6));
            u[1] = (char)(0x80 | (c & 0x3f));
            n = 2;
        } else
            u[0] = (char)c;
        if (t->overwrite && !oe_doc_has_sel(d) && oe_buf_char(&d->buf, d->caret) >= 0 &&
            oe_buf_char(&d->buf, d->caret) != '\n')
            oe_doc_select(d, d->caret, oe_doc_next(d, d->caret));
        oe_doc_insert(d, u, n, 1);
    }
    }
    refresh(&s);
}

/* ---- the mouse ---- */

static int in_text(int x, int y)
{
    return x >= A.gut_x && x < A.tx_x + A.tx_w && y >= A.tx_y - 2 && y < A.tx_y + A.rows * A.lh;
}

static void set_dragging(int on)
{
    A.dragging = on;
    ReportMouse(on, A.win);
    if (on)
        ModifyIDCMP(A.win, A.win->IDCMPFlags | IDCMP_INTUITICKS);
    else
        ModifyIDCMP(A.win, A.win->IDCMPFlags & ~IDCMP_INTUITICKS);
}

static void drag_to(int mx, int my)
{
    oe_doc *d = cur_doc();
    snap s;
    long p;
    take(&s);
    if (my < A.tx_y && d->top > 0)
        d->top--;
    else if (my >= A.tx_y + A.rows * A.lh && d->top + A.rows < oe_doc_lines(d))
        d->top++;
    p = oe_pos_at_xy(mx < A.tx_x ? A.tx_x : mx, my < A.tx_y ? A.tx_y : my >= A.tx_y + A.rows * A.lh ? A.tx_y + A.rows * A.lh - 1 : my);
    if (A.clicks >= 2) {
        /* Dragging after a double click selects whole words. */
        long a = d->anchor;
        oe_doc_select_word(d, p);
        if (p < a)
            oe_doc_select(d, A.last_click > a ? A.last_click : a, d->anchor);
        else
            oe_doc_select(d, A.last_click < a ? A.last_click : a, d->caret);
    } else
        oe_doc_set_caret(d, p, 1);
    refresh(&s);
}

static void mouse_down(int mx, int my, UWORD qual, ULONG secs, ULONG mics)
{
    oe_doc *d = cur_doc();
    int idx, kind = oe_hot_at(mx, my, &idx);
    snap s;
    switch (kind) {
    case HOT_TAB:
        oe_switch_tab(idx);
        return;
    case HOT_TABCLOSE:
        oe_close_tab(idx, 1);
        return;
    case HOT_TABPLUS:
        oe_new_tab(NULL, 0, 0, NULL);
        return;
    case HOT_ST_KIND:
        d->kind = d->kind + 1 >= A.nkinds ? -1 : d->kind + 1;
        d->lsok = 0;
        refresh_all();
        return;
    case HOT_ST_CS:
        oe_command(d->cs == OE_CS_UTF8 ? C_LATIN1 : C_UTF8);
        return;
    case HOT_ST_EOL:
        oe_command(d->eol == OE_EOL_LF ? C_CRLF : d->eol == OE_EOL_CRLF ? C_CR : C_LF);
        return;
    case HOT_ST_INS:
        oe_command(C_OVERWRITE);
        return;
    }
    if (!in_text(mx, my))
        return;
    take(&s);
    {
        long p = oe_pos_at_xy(mx, my);
        if (DoubleClick(A.last_secs, A.last_mics, secs, mics) && A.clicks > 0 &&
            oe_buf_line_of(&d->buf, p) == oe_buf_line_of(&d->buf, A.last_click))
            A.clicks = A.clicks >= 3 ? 1 : A.clicks + 1;
        else
            A.clicks = 1;
        A.last_secs = secs;
        A.last_mics = mics;
        if (A.clicks == 1) {
            oe_doc_set_caret(d, p, (qual & Q_SHIFT) != 0);
            A.last_click = d->anchor;
        } else if (A.clicks == 2) {
            oe_doc_select_word(d, p);
            A.last_click = d->caret;
        } else
            oe_doc_select_line(d, p);
    }
    refresh(&s);
    if (A.clicks < 3)
        set_dragging(1);
}

/* ---- events ---- */

static void gadget_event(struct Gadget *g, UWORD code, ULONG cl)
{
    switch (g->GadgetID) {
    case G_SCROLL:
        oe_scroll_to(code);
        break;
    case G_FIND:
        if (cl == IDCMP_GADGETUP && code == 27)
            hide_find();
        else if (cl == IDCMP_GADGETUP && code != 9) {
            read_find_fields();
            oe_find_next(0, 1);
            if (A.g_find)
                ActivateGadget(A.g_find, A.win, NULL);
        }
        break;
    case G_REPL:
        if (cl == IDCMP_GADGETUP && code == 27)
            hide_find();
        else if (cl == IDCMP_GADGETUP && code != 9)
            replace_one();
        break;
    case G_NEXT:
    case G_PREV:
        read_find_fields();
        oe_find_next(g->GadgetID == G_PREV, 1);
        break;
    case G_CASE:
        A.f_case = (g->Flags & GFLG_SELECTED) != 0;
        oe_draw_text();
        oe_count_hits();
        break;
    case G_WORD:
        A.f_word = (g->Flags & GFLG_SELECTED) != 0;
        oe_draw_text();
        oe_count_hits();
        break;
    case G_PAT:
        A.f_pat = (g->Flags & GFLG_SELECTED) != 0;
        oe_draw_text();
        oe_count_hits();
        break;
    case G_ALLTABS:
        A.f_alltabs = (g->Flags & GFLG_SELECTED) != 0;
        break;
    case G_CLOSE:
        hide_find();
        break;
    case G_REPL1:
        replace_one();
        break;
    case G_REPLALL:
        replace_all();
        break;
    }
}

static void menu_event(UWORD code)
{
    while (code != MENUNULL && !A.quit) {
        struct MenuItem *it = ItemAddress(A.menus, code);
        if (!it)
            break;
        oe_command((int)(LONG)GTMENUITEM_USERDATA(it));
        if (!A.win || !A.menus)
            break;
        code = it->NextSelect;
    }
}

static void main_event(struct IntuiMessage *m)
{
    ULONG cl = m->Class, secs = m->Seconds, mics = m->Micros;
    UWORD code = m->Code, qual = m->Qualifier;
    APTR ia = m->IAddress;
    int mx = m->MouseX, my = m->MouseY;
    GT_ReplyIMsg(m);
    switch (cl) {
    case IDCMP_CLOSEWINDOW:
        if (may_quit())
            A.quit = 1;
        break;
    case IDCMP_NEWSIZE:
        oe_layout();
        oe_draw_all();
        break;
    case IDCMP_REFRESHWINDOW:
        GT_BeginRefresh(A.win);
        oe_draw_all();
        GT_EndRefresh(A.win, TRUE);
        break;
    case IDCMP_ACTIVEWINDOW:
    case IDCMP_INACTIVEWINDOW:
        A.active = cl == IDCMP_ACTIVEWINDOW;
        if (cur_doc())
            oe_draw_lines(oe_doc_line(cur_doc()), oe_doc_line(cur_doc()));
        break;
    case IDCMP_MOUSEBUTTONS:
        if (code == SELECTDOWN)
            mouse_down(mx, my, qual, secs, mics);
        else if (code == SELECTUP && A.dragging)
            set_dragging(0);
        break;
    case IDCMP_MOUSEMOVE:
        if (A.dragging)
            drag_to(mx, my);
        else if (ia && ((struct Gadget *)ia)->GadgetID == G_SCROLL)
            oe_scroll_to(code);
        break;
    case IDCMP_INTUITICKS:
        if (A.dragging && (my < A.tx_y || my >= A.tx_y + A.rows * A.lh))
            drag_to(A.win->MouseX, A.win->MouseY);
        break;
    case IDCMP_GADGETDOWN:
    case IDCMP_GADGETUP:
        gadget_event((struct Gadget *)ia, code, cl);
        break;
    case IDCMP_MENUPICK:
        menu_event(code);
        break;
    case IDCMP_RAWKEY:
        raw_key(code, qual);
        break;
    case IDCMP_VANILLAKEY:
        typed((UBYTE)code, qual);
        break;
    }
}

/* ---- files from other starts ---- */

static void open_news(void)
{
    oe_openmsg *m;
    while ((m = (oe_openmsg *)GetMsg(A.open_port))) {
        int ok = 1;
        if (m->magic != OE_MAGIC) {
            ReplyMsg(&m->m);
            continue;
        }
        if (m->path[0] || (m->flags & OE_OPEN_NEW))
            ok = oe_new_tab(m->path[0] ? m->path : NULL, m->line, m->flags,
                            (m->flags & OE_OPEN_WAIT) ? m : NULL);
        if (!ok || !(m->flags & OE_OPEN_WAIT) || (!m->path[0] && !(m->flags & OE_OPEN_NEW))) {
            m->rc = ok ? 0 : RETURN_ERROR;
            ReplyMsg(&m->m);
        }
    }
    ScreenToFront(A.scr);
    WindowToFront(A.win);
    ActivateWindow(A.win);
}

/* A whole path for a name from this Shell's current drawer. */
static void full_path(const char *name, char *out, int size)
{
    BPTR l = Lock((CONST_STRPTR)name, SHARED_LOCK);
    out[0] = 0;
    if (l) {
        if (NameFromLock(l, (STRPTR)out, size)) {
            UnLock(l);
            return;
        }
        UnLock(l);
    }
    if (strchr(name, ':')) {
        snprintf(out, size, "%s", name);
        return;
    }
    if ((l = Lock((CONST_STRPTR)"", SHARED_LOCK))) {
        if (!NameFromLock(l, (STRPTR)out, size))
            out[0] = 0;
        UnLock(l);
    }
    AddPart((STRPTR)out, (STRPTR)name, size);
}

/* Hands files to the running copy and waits for its answers. -1 when none runs. */
static int hand_over(char **files, int n, long line, int flags)
{
    struct MsgPort *port, *reply;
    oe_openmsg *msgs;
    int i, sent = 0, rc = 0, count = n ? n : 1;
    if (!(reply = CreateMsgPort()))
        return -1;
    if (!(msgs = AllocVec(count * sizeof *msgs, MEMF_PUBLIC | MEMF_CLEAR))) {
        DeleteMsgPort(reply);
        return -1;
    }
    for (i = 0; i < count; i++) {
        msgs[i].m.mn_ReplyPort = reply;
        msgs[i].m.mn_Length = sizeof msgs[i];
        msgs[i].magic = OE_MAGIC;
        msgs[i].line = i == 0 ? line : 0;
        msgs[i].flags = flags;
        if (n)
            full_path(files[i], msgs[i].path, sizeof msgs[i].path);
    }
    Forbid();
    if ((port = FindPort((CONST_STRPTR)PORT_OPEN))) {
        for (i = 0; i < count; i++)
            PutMsg(port, &msgs[i].m);
        sent = count;
    }
    Permit();
    if (!sent) {
        FreeVec(msgs);
        DeleteMsgPort(reply);
        return -1;
    }
    for (i = 0; i < sent; i++) {
        oe_openmsg *r;
        WaitPort(reply);
        r = (oe_openmsg *)GetMsg(reply);
        if (r && r->rc > rc)
            rc = r->rc;
    }
    FreeVec(msgs);
    DeleteMsgPort(reply);
    return rc;
}

/* Starts the copy that keeps running, in the background, then waits for its port. */
static int start_server(const char *pubscreen)
{
    char prog[OE_PATH_MAX], cmd[OE_PATH_MAX + 80];
    BPTR in, out, dir = GetProgramDir();
    int i;
    prog[0] = 0;
    if (!dir || !NameFromLock(dir, (STRPTR)prog, sizeof prog))
        return 0;
    {
        char name[108];
        if (!GetProgramName((STRPTR)name, sizeof name))
            return 0;
        AddPart((STRPTR)prog, FilePart((STRPTR)name), sizeof prog);
    }
    if (pubscreen && pubscreen[0])
        snprintf(cmd, sizeof cmd, "\"%s\" SERVER PUBSCREEN \"%s\"", prog, pubscreen);
    else
        snprintf(cmd, sizeof cmd, "\"%s\" SERVER", prog);
    in = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE);
    out = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);
    if (!in || !out || SystemTags((CONST_STRPTR)cmd, SYS_Input, in, SYS_Output, out, SYS_Asynch, TRUE,
                                  NP_StackSize, 65536, TAG_DONE) != 0) {
        if (in)
            Close(in);
        if (out)
            Close(out);
        return 0;
    }
    for (i = 0; i < 100; i++) {
        struct MsgPort *p;
        Forbid();
        p = FindPort((CONST_STRPTR)PORT_OPEN);
        Permit();
        if (p)
            return 1;
        Delay(5);
    }
    return 0;
}

/* ---- the window ---- */

static int open_window(const char *pubscreen)
{
    int mode;
    if (!load_theme(&mode))
        return 0;
    if (!(A.scr = LockPubScreen((CONST_STRPTR)(pubscreen && pubscreen[0] ? pubscreen : NULL))) &&
        !(A.scr = LockPubScreen(NULL)))
        return 0;
    if (!(A.vi = GetVisualInfoA(A.scr, NULL)))
        return 0;
    if (!(A.ctx_ok = ogt_ctx_init(&A.ctx, A.scr, &A.theme, mode)))
        return 0;
    if (!(A.ui_font = ogt_open_font(&A.theme, A.scr, &A.ta)))
        return 0;
    A.fh = A.ui_font->tf_YSize;
    if (!open_text_font())
        return 0;
    oe_obtain_pens();
    if (!build_menus())
        return 0;
    A.win = OpenWindowTags(NULL, WA_Left, 0, WA_Top, A.scr->BarHeight + 1, WA_Width, A.scr->Width,
                           WA_Height, A.scr->Height - A.scr->BarHeight - 1, WA_MinWidth, 320, WA_MinHeight, 150,
                           WA_MaxWidth, ~0, WA_MaxHeight, ~0, WA_Title, (ULONG)"OpenEdit",
                           WA_ScreenTitle, (ULONG)"OpenEdit " OE_VERSION, WA_PubScreen, (ULONG)A.scr,
                           WA_NewLookMenus, TRUE, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
                           WA_SizeGadget, TRUE, WA_SizeBBottom, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
                           WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW | IDCMP_MOUSEBUTTONS |
                                         IDCMP_MOUSEMOVE | IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_MENUPICK |
                                         IDCMP_RAWKEY | IDCMP_VANILLAKEY | IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW |
                                         SCROLLERIDCMP | STRINGIDCMP | BUTTONIDCMP | CHECKBOXIDCMP,
                           TAG_DONE);
    if (!A.win)
        return 0;
    A.active = 1;
    SetFont(A.win->RPort, A.ui_font);
    SetMenuStrip(A.win, A.menus);
    ((struct Process *)FindTask(NULL))->pr_WindowPtr = A.win;
    return 1;
}

static void close_all(void)
{
    int i;
    for (i = 0; i < A.ntabs; i++)
        free_tab(A.tabs[i]);
    A.ntabs = 0;
    oe_rexx_close();
    if (A.open_port) {
        oe_openmsg *m;
        RemPort(A.open_port);
        while ((m = (oe_openmsg *)GetMsg(A.open_port))) {
            m->rc = RETURN_FAIL;
            ReplyMsg(&m->m);
        }
        DeleteMsgPort(A.open_port);
        A.open_port = NULL;
    }
    if (A.win) {
        ((struct Process *)FindTask(NULL))->pr_WindowPtr = NULL;
        ClearMenuStrip(A.win);
        oe_remove_gadgets();
        CloseWindow(A.win);
        A.win = NULL;
    }
    if (A.menus)
        FreeMenus(A.menus);
    A.menus = NULL;
    free(menu_def);
    if (A.tx_font)
        CloseFont(A.tx_font);
    if (A.ui_font)
        CloseFont(A.ui_font);
    if (A.ctx_ok)
        ogt_ctx_free(&A.ctx);
    if (A.theme.text)
        ogt_theme_free(&A.theme);
    if (A.vi)
        FreeVisualInfo(A.vi);
    if (A.scr)
        UnlockPubScreen(NULL, A.scr);
    for (i = 0; i < A.nkinds; i++)
        oe_kind_free(&A.kinds[i]);
    if (DiskfontBase)
        CloseLibrary(DiskfontBase);
}

static int run_server(char **files, int n, long line, int flags, const char *pubscreen)
{
    int i;
    /* Claim the port first, so a second start in the meantime hands over. */
    if (!(A.open_port = CreateMsgPort()))
        return RETURN_FAIL;
    A.open_port->mp_Node.ln_Name = (char *)PORT_OPEN;
    A.open_port->mp_Node.ln_Pri = 0;
    Forbid();
    if (FindPort((CONST_STRPTR)PORT_OPEN)) {
        Permit();
        DeleteMsgPort(A.open_port);
        A.open_port = NULL;
        return hand_over(files, n, line, flags) > 0 ? RETURN_ERROR : RETURN_OK;
    }
    AddPort(A.open_port);
    Permit();

    oe_prefs_load(&A.prefs);
    oe_load_kinds();
    if (!open_window(pubscreen)) {
        close_all();
        PutStr((CONST_STRPTR)"OpenEdit couldn't open its window.\n");
        return RETURN_FAIL;
    }
    oe_rexx_open();
    for (i = 0; i < n; i++) {
        char path[OE_PATH_MAX];
        full_path(files[i], path, sizeof path);
        oe_new_tab(path, i == 0 ? line : 0, flags & ~OE_OPEN_WAIT, NULL);
    }
    if (!A.ntabs)
        oe_new_tab(NULL, 0, 0, NULL);
    refresh_all();

    while (!A.quit) {
        ULONG got = Wait((1UL << A.win->UserPort->mp_SigBit) | (1UL << A.open_port->mp_SigBit) | SIGBREAKF_CTRL_C |
                         (A.rexx_port ? 1UL << A.rexx_port->mp_SigBit : 0));
        struct IntuiMessage *m;
        if (got & SIGBREAKF_CTRL_C) {
            if (may_quit())
                A.quit = 1;
        }
        if (got & (1UL << A.open_port->mp_SigBit))
            open_news();
        if (A.rexx_port && (got & (1UL << A.rexx_port->mp_SigBit)))
            oe_rexx_news();
        while (!A.quit && A.win && (m = GT_GetIMsg(A.win->UserPort)))
            main_event(m);
    }
    close_all();
    return RETURN_OK;
}

/* ---- starting ---- */

enum { A_FILES, A_LINE, A_WAIT, A_READONLY, A_NEW, A_PUBSCREEN, A_SERVER, A_COUNT };

static int oe_main(void)
{
    LONG args[A_COUNT];
    struct RDArgs *rda = NULL;
    char **files = NULL, *wbfiles[16], wbpaths[16][OE_PATH_MAX];
    int n = 0, flags = 0, rc;
    long line = 0;
    const char *pubscreen = NULL;

    memset(args, 0, sizeof args);
    if (Cli()) {
        if (!(rda = ReadArgs((CONST_STRPTR)"FILES/M,LINE/N,WAIT/S,READONLY/S,NEW/S,PUBSCREEN/K,SERVER/S", args, NULL))) {
            PrintFault(IoErr(), (CONST_STRPTR)"OpenEdit");
            return RETURN_ERROR;
        }
        if ((files = (char **)args[A_FILES]))
            while (files[n])
                n++;
        if (args[A_LINE])
            line = *(LONG *)args[A_LINE];
        flags = (args[A_WAIT] ? OE_OPEN_WAIT : 0) | (args[A_READONLY] ? OE_OPEN_READONLY : 0) |
                (args[A_NEW] ? OE_OPEN_NEW : 0);
        pubscreen = (const char *)args[A_PUBSCREEN];
    } else if (_WBenchMsg) {
        /* Project icons whose default tool is OpenEdit. */
        int i;
        for (i = 1; i < _WBenchMsg->sm_NumArgs && n < 16; i++) {
            struct WBArg *wa = &_WBenchMsg->sm_ArgList[i];
            if (!wa->wa_Lock || !NameFromLock(wa->wa_Lock, (STRPTR)wbpaths[n], OE_PATH_MAX))
                continue;
            AddPart((STRPTR)wbpaths[n], (CONST_STRPTR)wa->wa_Name, OE_PATH_MAX);
            wbfiles[n] = wbpaths[n];
            n++;
        }
        files = wbfiles;
    }

    if (args[A_SERVER] || !Cli()) {
        rc = run_server(files, n, line, flags, pubscreen);
    } else if ((rc = hand_over(files, n, line, flags)) < 0) {
        /* None running: start one in the background and hand over to it. */
        if (start_server(pubscreen) && (rc = hand_over(files, n, line, flags)) >= 0)
            rc = rc ? RETURN_ERROR : RETURN_OK;
        else
            rc = run_server(files, n, line, flags, pubscreen);
    } else
        rc = rc ? RETURN_ERROR : RETURN_OK;
    if (rda)
        FreeArgs(rda);
    (void)version;
    return rc;
}

int main(void)
{
    return oe_main_with_stack(oe_main, 65536);
}
