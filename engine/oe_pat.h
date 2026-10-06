/* oe_pat: AmigaDOS-style patterns, matched against text in memory.
 *
 * dos.library's MatchPattern only says whether a whole string matches; find
 * needs where a match starts and ends inside a line, so OpenEdit has its own
 * matcher for the same syntax:
 *   ?        any one character
 *   #x       any number of x (x is a character, ?, a [class] or a (group))
 *   *        the same as #?
 *   [a-z]    one of a class; [~a-z] anything else
 *   (a|b)    either; % matches nothing, as in (s|%)
 *   'x       x itself, for the characters above
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OE_PAT_H
#define OE_PAT_H

/* The longest match of pat (length pn) starting exactly at s; returns its end,
 * or NULL. nocase compares letters (ISO-8859-1) without case. */
const char *oe_pat_at(const char *pat, int pn, const char *s, const char *se, int nocase);

/* Whole-string match, as MatchPattern. */
int oe_pat_whole(const char *pat, const char *s, int nocase);

/* 1 if a pattern uses any of the special characters. */
int oe_pat_is_wild(const char *pat);

/* ISO-8859-1 lower case, and comparing without case. */
int oe_lower(int c);
int oe_strnicmp(const char *a, const char *b, long n);
int oe_stricmp(const char *a, const char *b);

#endif
