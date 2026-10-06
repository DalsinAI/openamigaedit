/* oe_prefs: OpenEdit's settings, in ENV:OpenEdit/Settings (Use) and
 * ENVARC:OpenEdit/Settings (Save), one setting a line:
 *   font topaz.font 8          (no font line: the system's text font)
 *   tabs 8
 *   spaces off
 *   autoindent on
 *   numbers on
 *   curline on
 *   colours on
 *   scheme Classic
 *   light.string #a0461f       (a colour per class, light and dark)
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oe_app.h"

static const char *const class_key[OE_C_COUNT] = {
    "text", "command", "keyword", "string", "variable", "number", "label", "comment"
};

/* Classic and Open 4, light then dark; text (0) follows the theme. */
static const char *const schemes[2][2][OE_C_COUNT] = {
    { { "#121825", "#23488f", "#8a2f8f", "#a0461f", "#11807a", "#9a5b00", "#b3261e", "#5f7d5a" },
      { "#e5e9f0", "#8fb3f0", "#d59ad8", "#e3a07c", "#6fd1c9", "#e0b45c", "#f08a80", "#87a882" } },
    { { "#121825", "#2d5ea8", "#6b3fa0", "#b5532a", "#2a7f62", "#a86a00", "#c0392b", "#7a8794" },
      { "#e5e9f0", "#7fa8e8", "#b99be0", "#e8a07a", "#6cc7a8", "#e8c070", "#f28b82", "#8a96a3" } },
};

void oe_prefs_scheme(oe_prefs *p, int scheme)
{
    int m, c;
    if (scheme < 0 || scheme > 1)
        scheme = 0;
    p->scheme = scheme;
    for (m = 0; m < 2; m++)
        for (c = 0; c < OE_C_COUNT; c++)
            ogt_parse_colour(schemes[scheme][m][c], &p->pen[m][c]);
}

void oe_prefs_default(oe_prefs *p)
{
    memset(p, 0, sizeof *p);
    p->tabw = 8;
    p->autoindent = 1;
    p->numbers = 1;
    p->curline = 1;
    p->colours = 1;
    oe_prefs_scheme(p, 0);
}

int oe_read_small(const char *path, char *buf, int size)
{
    BPTR f = Open((CONST_STRPTR)path, MODE_OLDFILE);
    LONG n;
    if (!f)
        return 0;
    n = Read(f, buf, size - 1);
    Close(f);
    buf[n > 0 ? n : 0] = 0;
    return 1;
}

char *oe_load_text(const char *path, long max, long *n)
{
    BPTR f = Open((CONST_STRPTR)path, MODE_OLDFILE);
    struct FileInfoBlock *fib;
    char *buf = NULL;
    *n = 0;
    if (!f)
        return NULL;
    if ((fib = AllocDosObject(DOS_FIB, NULL))) {
        if (ExamineFH(f, fib) && fib->fib_Size >= 0 && fib->fib_Size <= max && (buf = malloc(fib->fib_Size + 1))) {
            LONG got = fib->fib_Size ? Read(f, buf, fib->fib_Size) : 0;
            if (got < 0) {
                free(buf);
                buf = NULL;
            } else {
                buf[got] = 0;
                *n = got;
            }
        }
        FreeDosObject(DOS_FIB, fib);
    }
    Close(f);
    return buf;
}

static int on(const char *v)
{
    return !(strncmp(v, "off", 3) == 0 || strncmp(v, "no", 2) == 0 || strncmp(v, "0", 1) == 0);
}

void oe_prefs_load(oe_prefs *p)
{
    char text[2048], *s, *e;
    oe_prefs_default(p);
    if (!oe_read_small("ENV:OpenEdit/Settings", text, sizeof text) &&
        !oe_read_small("ENVARC:OpenEdit/Settings", text, sizeof text))
        return;
    for (s = text; *s; s = *e ? e + 1 : e) {
        char *v;
        int c, m;
        for (e = s; *e && *e != '\n'; e++)
            ;
        if (*e)
            *e = 0;
        else
            e = s + strlen(s);
        if (!(v = strchr(s, ' ')))
            continue;
        *v++ = 0;
        while (*v == ' ')
            v++;
        if (strcmp(s, "font") == 0) {
            char *sz = strrchr(v, ' ');
            if (sz) {
                *sz++ = 0;
                p->font_size = atoi(sz);
            }
            snprintf(p->font, sizeof p->font, "%s", v);
        } else if (strcmp(s, "tabs") == 0) {
            p->tabw = atoi(v);
            if (p->tabw < 1 || p->tabw > 16)
                p->tabw = 8;
        } else if (strcmp(s, "spaces") == 0)
            p->spaces = on(v);
        else if (strcmp(s, "autoindent") == 0)
            p->autoindent = on(v);
        else if (strcmp(s, "numbers") == 0)
            p->numbers = on(v);
        else if (strcmp(s, "curline") == 0)
            p->curline = on(v);
        else if (strcmp(s, "colours") == 0)
            p->colours = on(v);
        else if (strcmp(s, "scheme") == 0)
            oe_prefs_scheme(p, strncmp(v, "Open", 4) == 0 ? 1 : 0);
        else
            for (m = 0; m < 2; m++)
                for (c = 1; c < OE_C_COUNT; c++) {
                    char key[24];
                    snprintf(key, sizeof key, "%s.%s", m ? "dark" : "light", class_key[c]);
                    if (strcmp(s, key) == 0)
                        ogt_parse_colour(v, &p->pen[m][c]);
                }
    }
}

static void make_dir(const char *dir)
{
    BPTR l = Lock((CONST_STRPTR)dir, SHARED_LOCK);
    if (l)
        UnLock(l);
    else if ((l = CreateDir((CONST_STRPTR)dir)))
        UnLock(l);
}

void oe_prefs_save(const oe_prefs *p, int envarc)
{
    char text[2048];
    int n = 0, i, m, c;
    BPTR f;
    if (p->font[0])
        n += snprintf(text + n, sizeof text - n, "font %s %d\n", p->font, p->font_size);
    n += snprintf(text + n, sizeof text - n, "tabs %d\nspaces %s\nautoindent %s\nnumbers %s\ncurline %s\ncolours %s\nscheme %s\n",
                  p->tabw, p->spaces ? "on" : "off", p->autoindent ? "on" : "off", p->numbers ? "on" : "off",
                  p->curline ? "on" : "off", p->colours ? "on" : "off", p->scheme ? "Open 4" : "Classic");
    for (m = 0; m < 2; m++)
        for (c = 1; c < OE_C_COUNT; c++)
            n += snprintf(text + n, sizeof text - n, "%s.%s #%02x%02x%02x\n", m ? "dark" : "light", class_key[c],
                          p->pen[m][c].r, p->pen[m][c].g, p->pen[m][c].b);
    for (i = envarc ? 0 : 1; i < 2; i++) {
        const char *root = i ? "ENV:" : "ENVARC:";
        char path[64];
        snprintf(path, sizeof path, "%sOpenEdit", root);
        make_dir(path);
        snprintf(path, sizeof path, "%sOpenEdit/Settings", root);
        if ((f = Open((CONST_STRPTR)path, MODE_NEWFILE))) {
            Write(f, text, n);
            Close(f);
        }
    }
}
