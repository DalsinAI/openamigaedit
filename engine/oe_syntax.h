/* oe_syntax: colour kinds. A kind is a small text file in
 * ENVARC:OpenEdit/Kinds/ that says which files it is for and how to colour
 * them; adding one needs no rebuild. Plain C, tested on the host.
 *
 * A kind file, one setting a line (a line starting with # is a note):
 *   name      AmigaDOS script
 *   files     #?.script|#?.dos          which names (oe_pat.h patterns)
 *   paths     S:#?|SYS:S/#?             or which whole paths
 *   firstline #!#?                      or a pattern for the first line
 *   case      off                       keywords ignore case
 *   comment   ;                         a comment to the end of the line (two allowed)
 *   comment0  *                         a comment only in the first column
 *   block     <!-- -->                  a comment that can span lines
 *   nest      on                        block comments nest (ARexx)
 *   strings   "'                        string quotes
 *   escape    *                         escapes the next character in a string
 *   variable  $                         $name, $(name) and ${name}
 *   directive .                         .key or #include at a line's start
 *   labels    after LAB SKIP            the word after these is a label
 *   labels    colon                     name: at a line's start is a label
 *   labels    column0                   a word in the first column is a label
 *   tags      on                        <tag ...> (HTML)
 *   inline    @{ }                      @{b} (AmigaGuide)
 *   numbers   on
 *   suffix    .                         word.w looks up "word" (assembler)
 *   keywords  IF ELSE ENDIF ...         (any number of these lines)
 *   commands  Assign Path ...
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OE_SYNTAX_H
#define OE_SYNTAX_H

#include "oe_doc.h"

/* What each character is coloured as. */
enum { OE_C_TEXT = 0, OE_C_COMMAND, OE_C_KEYWORD, OE_C_STRING, OE_C_VARIABLE, OE_C_NUMBER,
       OE_C_LABEL, OE_C_COMMENT, OE_C_COUNT };

typedef struct oe_kind {
    char name[40];
    char files[160], paths[160], firstline[64];
    int nocase, nest, numbers, tags, label_colon, label_col0;
    char comment[2][8], comment0;
    char block[2][8];
    char strings[8], escape, variable, directive, suffix;
    char inl[2][4];
    char *pool;                 /* words, 0-separated, owned */
    long pooln, poolcap;
    struct oe_word {
        long off;               /* into pool */
        int cls;
    } *words;                   /* sorted */
    int nwords, wcap;
    char after[4][12];          /* words that make the next one a label */
    int nafter;
} oe_kind;

/* Reads a kind from its file's text. 1, or 0 with the reason in err. */
int oe_kind_parse(oe_kind *k, const char *text, char *err, int errlen);
void oe_kind_free(oe_kind *k);

/* Which of n kinds fits a file: by path, name, then first line. -1 none. */
int oe_kind_pick(oe_kind *kinds, int n, const char *path, const char *first, int firstn);

/* Colours one line: cls gets a class per byte. state is what the line
 * before left open (0 at the start of a file); returns what this one leaves. */
int oe_syn_line(const oe_kind *k, const char *s, int n, int state, unsigned char *cls);

/* The state at the start of a line of a document, worked out from the last
 * line known and remembered (oe_doc's lstate). */
int oe_syn_state(oe_doc *d, const oe_kind *k, long line);

#endif
