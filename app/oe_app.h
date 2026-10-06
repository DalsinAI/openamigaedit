/* oe_app: what the parts of the OpenEdit window share. One window with a
 * tab per file; the text area is drawn by OpenEdit itself, everything else
 * is GadTools, drawn in the OpenLook theme through OpenGadTools' drawing
 * code (linked in, no extra library).
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OE_APP_H
#define OE_APP_H

#include <exec/types.h>
#include <exec/ports.h>
#include <intuition/intuition.h>
#include <graphics/text.h>

#include "ogt_theme.h"
#include "ogt_draw.h"
#include "oe_doc.h"
#include "oe_syntax.h"

#define OE_VERSION "0.1"
#define OE_DATE "6.10.2026"
#define MAX_DOCS 32
#define MAX_KINDS 32
#define PORT_OPEN "OpenEdit"        /* a second start hands its files over here */
#define PORT_REXX "OPENEDIT"        /* ARexx */
#define OE_MAGIC 0x4f456431UL       /* "OEd1" */

/* Settings (ENV:OpenEdit/Settings). */
typedef struct oe_prefs {
    char font[64];                  /* "" for the system's text font */
    int font_size;
    int tabw, spaces, autoindent;
    int numbers, curline, colours;
    int scheme;                     /* 0 Classic, 1 Open 4 */
    ogt_rgb pen[2][OE_C_COUNT];     /* light, dark: one colour per class */
} oe_prefs;

/* A file handed over by another start; replied when its tab closes (WAIT). */
typedef struct oe_openmsg {
    struct Message m;
    ULONG magic;
    LONG line;
    LONG flags;                     /* OE_OPEN_* */
    LONG rc;
    char path[OE_PATH_MAX];
} oe_openmsg;

#define OE_OPEN_WAIT 1
#define OE_OPEN_READONLY 2
#define OE_OPEN_NEW 4

typedef struct oe_tab {
    oe_doc d;
    oe_openmsg *waiter;
    long saved_secs;                /* when last saved, for the status line */
    int overwrite;
} oe_tab;

/* Places the mouse can press that aren't gadgets. */
enum { HOT_TAB = 1, HOT_TABCLOSE, HOT_TABPLUS, HOT_ST_KIND, HOT_ST_CS, HOT_ST_EOL, HOT_ST_INS };
typedef struct oe_hot {
    int x, y, w, h, kind, idx;
} oe_hot;
#define MAX_HOT 48

typedef struct oe_app {
    struct Screen *scr;
    APTR vi;
    struct Window *win;
    struct Menu *menus;
    struct Gadget *glist;
    struct TextFont *ui_font, *tx_font;
    struct TextAttr ta;             /* the UI font, for gadgets */
    ogt_theme theme;
    ogt_ctx ctx;
    int ctx_ok;
    int fh;                         /* UI font height */
    int lh, cw;                     /* text line height, character width */
    int bold_only;                  /* too few colours: classes as bold or plain */
    LONG cpen[OE_C_COUNT];          /* text pens per class */
    LONG pen_bg, pen_cur, pen_sel, pen_hit, pen_hit_on, pen_gut, pen_gut_t, pen_text, pen_caret;
    oe_prefs prefs;
    oe_kind kinds[MAX_KINDS];
    int nkinds;
    oe_tab *tabs[MAX_DOCS];
    int ntabs, cur, first_tab, untitled;
    oe_hot hots[MAX_HOT];
    int nhot;
    /* layout */
    int tab_y, tab_h, tx_x, tx_y, tx_w, tx_h, gut_x, gut_w, rows, cols;
    int find_y, find_h, stat_y, stat_h;
    int find_on;
    struct Gadget *g_scroll, *g_find, *g_repl, *g_case, *g_word, *g_pat, *g_alltabs, *g_count;
    int f_case, f_word, f_pat, f_alltabs;
    char find_text[128], repl_text[128];
    long hit_total, hit_index;
    int active;                     /* the window is active: the caret is solid */
    int dragging;
    long last_secs, last_mics;
    int clicks;
    long last_click;
    struct MsgPort *open_port, *rexx_port;
    int quit;
} oe_app;

extern oe_app A;

/* openedit.c */
oe_tab *cur_tab(void);
oe_doc *cur_doc(void);
int oe_new_tab(const char *path, long line, int flags, oe_openmsg *waiter);
int oe_close_tab(int i, int ask);
void oe_switch_tab(int i);
void oe_command(int cmd);
void oe_note(const char *title, const char *fmt, ...);
int oe_find_next(int backward, int wrap_note);
void oe_set_title(void);
void oe_after_edit(long old_lines, long old_top, int old_left, long old_caret_line);

/* oe_draw.c */
void oe_layout(void);
void oe_draw_all(void);
void oe_draw_tabs(void);
void oe_draw_text(void);
void oe_draw_lines(long from, long to);
void oe_draw_status(void);
void oe_update_scroller(void);
void oe_scroll_to(long top);
void oe_show_caret(void);       /* scrolls so the caret is in view; 1 if it scrolled */
int oe_caret_in_view(void);
long oe_pos_at_xy(int x, int y);
void oe_remove_gadgets(void);
void oe_add_hot(int x, int y, int w, int h, int kind, int idx);
int oe_hot_at(int x, int y, int *idx);
void oe_obtain_pens(void);
void oe_release_pens(void);
void oe_count_hits(void);
void oe_draw_restate(long line);    /* redraws the lines after line whose colour state changed */

/* oe_file.c */
int oe_load_file(oe_doc *d, const char *path);
int oe_save_file(oe_doc *d, const char *path, char *err, int errlen);
int oe_ask_file(int save, const char *title, char *path, int size);
int oe_clip_write(const char *s, long n);
char *oe_clip_read(long *n);
void oe_load_kinds(void);
void oe_pick_kind(oe_doc *d);

/* oe_req.c */
int oe_req_string(const char *title, const char *question, const char *ok, char *buf, int size, int numeric);
int oe_req_three(const char *title, const char *text, const char *gads);
int oe_req_unsaved(int *ticked);    /* 0 cancel, 1 don't save, 2 save ticked */
int oe_req_settings(void);

/* oe_prefs.c */
void oe_prefs_default(oe_prefs *p);
void oe_prefs_load(oe_prefs *p);
void oe_prefs_save(const oe_prefs *p, int envarc);
void oe_prefs_scheme(oe_prefs *p, int scheme);
int oe_read_small(const char *path, char *buf, int size);
char *oe_load_text(const char *path, long max, long *n);

/* oe_rexx.c */
int oe_rexx_open(void);
void oe_rexx_close(void);
void oe_rexx_news(void);

/* The main window's gadgets. */
enum { G_SCROLL = 1, G_FIND, G_PREV, G_NEXT, G_COUNT, G_CASE, G_WORD, G_PAT, G_CLOSE, G_REPL, G_REPL1, G_REPLALL,
       G_ALLTABS };

/* Commands: menu items, keys and ARexx. */
enum { C_NEW = 1, C_OPEN, C_SAVE, C_SAVEAS, C_SAVEALL, C_CLOSE, C_REVERT, C_INSFILE, C_ABOUT, C_QUIT,
       C_UNDO, C_REDO, C_CUT, C_COPY, C_PASTE, C_DELETE, C_SELALL, C_INDENT, C_OUTDENT, C_OVERWRITE,
       C_FIND, C_FINDNEXT, C_FINDPREV, C_REPLACE, C_REPLACEALL, C_GOTO, C_FINDCLOSE,
       C_NUMBERS, C_CURLINE, C_COLOURS, C_PLAIN, C_LATIN1, C_UTF8, C_LF, C_CRLF, C_CR, C_READONLY,
       C_SETTINGS, C_NEXTTAB, C_PREVTAB, C_KIND0 = 100 };

#endif
