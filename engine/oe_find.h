/* oe_find: find and replace in a document, a line at a time (a match never
 * crosses a line end). Plain C, tested on the host.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OE_FIND_H
#define OE_FIND_H

#include "oe_doc.h"

#define OE_F_CASE  1        /* match case */
#define OE_F_WORD  2        /* whole words only */
#define OE_F_PAT   4        /* AmigaDOS pattern (oe_pat.h) */

/* The next match at or after from (or, backward, ending at or before from).
 * Wraps around the end once. 1 with the match in *a..*b, or 0. */
int oe_find(oe_doc *d, const char *what, int flags, long from, int backward, long *a, long *b);

/* How many matches there are, and which one (1-based) starts at at. */
long oe_find_count(oe_doc *d, const char *what, int flags, long at, long *index);

/* All matches on one line, for marking them: up to max pairs of offsets
 * within the line. Returns the count. */
int oe_find_line(oe_doc *d, const char *what, int flags, long line, long *starts, long *ends, int max);

/* Replaces every match; one undo step. Returns how many. */
long oe_replace_all(oe_doc *d, const char *what, int flags, const char *with);

#endif
