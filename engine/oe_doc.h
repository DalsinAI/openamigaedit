/* oe_doc: one open file. Its text, the caret and selection, undo and redo,
 * and how it is read from and written back to disk (character set, line
 * ends). Plain C, tested on the host; the window (app/) draws it.
 *
 * Text is kept as the file's own bytes, with line ends turned into LF while
 * it is open and turned back when saved. In a UTF-8 file the caret steps over
 * whole characters; a character is one column wide whatever its bytes.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OE_DOC_H
#define OE_DOC_H

#include "oe_buf.h"

enum { OE_EOL_LF = 0, OE_EOL_CRLF, OE_EOL_CR };
enum { OE_CS_LATIN1 = 0, OE_CS_UTF8 };

/* Caret moves. */
enum { OE_LEFT = 1, OE_RIGHT, OE_UP, OE_DOWN, OE_WORDL, OE_WORDR, OE_HOME, OE_END,
       OE_PGUP, OE_PGDN, OE_TOP, OE_BOTTOM };

#define OE_PATH_MAX 256
#define OE_UNDO_MAX 4000

typedef struct oe_undo {
    long pos, n;
    char *text;
    long caret, anchor;         /* before the edit */
    unsigned group;
    unsigned char ins;          /* 1 inserted, 0 deleted */
    unsigned char typing;       /* 1 typed, 2 backspace, 3 delete: may merge */
} oe_undo;

typedef struct oe_doc {
    oe_buf buf;
    long caret, anchor;         /* the selection is between them */
    int goal;                   /* column up and down aim for; -1 none */
    int tabw, spaces, autoindent;
    int cs, bom, eol;
    int readonly;
    oe_undo *u;
    long un, ucap, uat, saved_at;   /* saved_at -1: the saved text is gone from undo */
    unsigned group_next, group_fixed;
    int nest;                   /* oe_doc_begin depth */
    int typing;                 /* the last edit may take the next keystroke */
    long top;                   /* first line shown */
    int left;                   /* first column shown */
    int kind;                   /* colour kind, -1 plain */
    int *lstate;                /* colour state at each line's start */
    long lscap, lsok;           /* lstate is right for lines below lsok */
    long edits;
    char path[OE_PATH_MAX];     /* "" until saved */
    char name[32];              /* "Untitled 1" for a new file */
} oe_doc;

int oe_doc_init(oe_doc *d);
void oe_doc_free(oe_doc *d);

/* Takes a file's bytes: finds the character set and line ends, clears undo. */
int oe_doc_load(oe_doc *d, const char *data, long n);
/* The bytes to write: size first, then fill a buffer that big. */
long oe_doc_save_size(oe_doc *d);
void oe_doc_save_bytes(oe_doc *d, char *out);
void oe_doc_mark_saved(oe_doc *d);
int oe_doc_modified(const oe_doc *d);

long oe_doc_len(const oe_doc *d);
long oe_doc_lines(const oe_doc *d);
long oe_doc_line(const oe_doc *d);              /* the caret's line */

/* Selection. */
int oe_doc_has_sel(const oe_doc *d);
void oe_doc_sel(const oe_doc *d, long *a, long *b);
char *oe_doc_text(oe_doc *d, long a, long b, long *n);     /* malloc'd, 0-ended */
void oe_doc_set_caret(oe_doc *d, long pos, int extend);
void oe_doc_select(oe_doc *d, long a, long b);
void oe_doc_select_all(oe_doc *d);
void oe_doc_select_word(oe_doc *d, long pos);
void oe_doc_select_line(oe_doc *d, long pos);
void oe_doc_move(oe_doc *d, int how, int extend, int page);

/* Editing (each 1, or 0 when out of memory or read-only). */
int oe_doc_insert(oe_doc *d, const char *s, long n, int typing);
int oe_doc_delete_sel(oe_doc *d);
int oe_doc_backspace(oe_doc *d);
int oe_doc_delete(oe_doc *d);
int oe_doc_newline(oe_doc *d);
int oe_doc_tab(oe_doc *d, int outdent);
int oe_doc_replace(oe_doc *d, long a, long b, const char *s, long n);
int oe_doc_undo(oe_doc *d);
int oe_doc_redo(oe_doc *d);
int oe_doc_can_undo(const oe_doc *d);
int oe_doc_can_redo(const oe_doc *d);
/* Everything between begin and end undoes as one step. */
void oe_doc_begin(oe_doc *d);
void oe_doc_end(oe_doc *d);
void oe_doc_break_typing(oe_doc *d);

/* Columns, with tabs expanded and a UTF-8 character one column. */
int oe_doc_col(oe_doc *d, long pos);
long oe_doc_pos_at(oe_doc *d, long line, int col);
long oe_doc_next(const oe_doc *d, long pos);
long oe_doc_prev(const oe_doc *d, long pos);

/* One line as it is shown, from column `left`, at most max columns: out
 * gets the characters (tabs as spaces, UTF-8 as ISO-8859-1 or 0x7f for one
 * the font can't have), src the byte offset within the line each column
 * comes from. Returns the columns written. */
int oe_doc_show(oe_doc *d, long line, int left, int max, char *out, int *src);

int oe_is_word(int c);

#endif
