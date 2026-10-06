/* oe_buf: the text of one file, as a gap buffer with a line index.
 *
 * Plain C with no Amiga calls, tested on the host (tests/host). Positions
 * are byte offsets into the text as if the gap were not there. The line
 * index holds where every line starts, so going from a line number to its
 * text is one lookup; an edit moves the starts after it (one pass over the
 * index, which stays quick on a 68040 for files of many thousand lines).
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OE_BUF_H
#define OE_BUF_H

typedef struct oe_buf {
    char *b;
    long cap, gs, ge;       /* storage, gap start and end */
    long *ls;               /* line starts; ls[0] is 0 */
    long nl, lcap;          /* lines (at least 1) */
} oe_buf;

int oe_buf_init(oe_buf *b);
void oe_buf_free(oe_buf *b);

long oe_buf_len(const oe_buf *b);
int oe_buf_char(const oe_buf *b, long pos);         /* 0..255, or -1 outside */
/* Copies n bytes from pos to dst (no terminator). Returns bytes copied. */
long oe_buf_copy(const oe_buf *b, long pos, long n, char *dst);

/* 1, or 0 when out of memory (the text is then unchanged). */
int oe_buf_insert(oe_buf *b, long pos, const char *s, long n);
void oe_buf_delete(oe_buf *b, long pos, long n);

long oe_buf_lines(const oe_buf *b);
long oe_buf_line_of(const oe_buf *b, long pos);
long oe_buf_line_start(const oe_buf *b, long line);
long oe_buf_line_len(const oe_buf *b, long line);   /* without its newline */

/* A pointer to n contiguous bytes from pos: moves the gap out of the way
 * when the range straddles it. */
const char *oe_buf_span(oe_buf *b, long pos, long n);

#endif
