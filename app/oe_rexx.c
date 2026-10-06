/* oe_rexx: the OPENEDIT ARexx port, so other programs can drive OpenEdit.
 *
 *   OPEN file [LINE n]   NEW            SAVE           SAVEAS file
 *   CLOSE [FORCE]        GOTO line [col]               TAB n
 *   FIND text            REPLACEALL text with          INSERT text
 *   GETTEXT              GETLINE [n]    GETFILE        GETPOS
 *   FRONT                QUIT [FORCE]
 *
 * Text with spaces goes in quotes. Results come back in RESULT (OPTIONS
 * RESULTS); a command that can't be done returns 10, FIND that finds
 * nothing returns 5.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <exec/ports.h>
#include <rexx/storage.h>
#include <rexx/rxslib.h>
#include <rexx/errors.h>
#include <proto/exec.h>
#include <proto/intuition.h>
/* The base is ours, typed as the NDK has it whichever way its proto does. */
#define __NOLIBBASE__
#include <proto/rexxsyslib.h>
#undef __NOLIBBASE__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oe_app.h"
#include "oe_find.h"
#include "oe_pat.h"

struct RxsLib *RexxSysBase;

int oe_rexx_open(void)
{
    if (!(RexxSysBase = (struct RxsLib *)OpenLibrary((CONST_STRPTR)"rexxsyslib.library", 36)))
        return 0;
    if (!(A.rexx_port = CreateMsgPort()))
        return 0;
    A.rexx_port->mp_Node.ln_Name = (char *)PORT_REXX;
    A.rexx_port->mp_Node.ln_Pri = 0;
    Forbid();
    if (FindPort((CONST_STRPTR)PORT_REXX)) {
        Permit();
        DeleteMsgPort(A.rexx_port);
        A.rexx_port = NULL;
        return 0;
    }
    AddPort(A.rexx_port);
    Permit();
    return 1;
}

void oe_rexx_close(void)
{
    if (A.rexx_port) {
        struct RexxMsg *m;
        RemPort(A.rexx_port);
        while ((m = (struct RexxMsg *)GetMsg(A.rexx_port))) {
            m->rm_Result1 = RC_FATAL;
            m->rm_Result2 = 0;
            ReplyMsg((struct Message *)m);
        }
        DeleteMsgPort(A.rexx_port);
        A.rexx_port = NULL;
    }
    if (RexxSysBase)
        CloseLibrary((struct Library *)RexxSysBase);
    RexxSysBase = NULL;
}

/* The next word, or "quoted text"; 0-ends it in place. */
static char *arg(char **s)
{
    char *a;
    while (**s == ' ' || **s == '\t')
        (*s)++;
    if (!**s)
        return NULL;
    if (**s == '"' || **s == '\'') {
        char q = *(*s)++;
        a = *s;
        while (**s && **s != q)
            (*s)++;
    } else {
        a = *s;
        while (**s && **s != ' ' && **s != '\t')
            (*s)++;
    }
    if (**s)
        *(*s)++ = 0;
    return a;
}

static void after_change(void)
{
    oe_set_title();
    oe_layout();
    oe_draw_all();
}

/* Runs one command: the return code, and *res a malloc'd result or NULL. */
static long run(char *cmd, char **res, long *resn)
{
    char *s = cmd, *c = arg(&s), *a;
    oe_doc *d = cur_doc();
    *res = NULL;
    *resn = 0;
    if (!c)
        return RC_ERROR;
    if (!oe_stricmp(c, "OPEN")) {
        long line = 0;
        if (!(a = arg(&s)))
            return RC_ERROR;
        {
            char *k = arg(&s), *v = arg(&s);
            if (k && v && !oe_stricmp(k, "LINE"))
                line = atol(v);
        }
        return oe_new_tab(a, line, 0, NULL) ? 0 : RC_ERROR;
    }
    if (!oe_stricmp(c, "NEW"))
        return oe_new_tab(NULL, 0, OE_OPEN_NEW, NULL) ? 0 : RC_ERROR;
    if (!oe_stricmp(c, "FRONT")) {
        WindowToFront(A.win);
        ScreenToFront(A.scr);
        ActivateWindow(A.win);
        return 0;
    }
    if (!oe_stricmp(c, "QUIT")) {
        if ((a = arg(&s)) && !oe_stricmp(a, "FORCE")) {
            int i;
            for (i = 0; i < A.ntabs; i++)
                oe_doc_mark_saved(&A.tabs[i]->d);
        }
        A.quit = 1;
        return 0;
    }
    if (!d)
        return RC_ERROR;
    if (!oe_stricmp(c, "SAVE")) {
        char err[128];
        if (!d->path[0] || !oe_save_file(d, d->path, err, sizeof err))
            return RC_ERROR;
        after_change();
        return 0;
    }
    if (!oe_stricmp(c, "SAVEAS")) {
        char err[128];
        if (!(a = arg(&s)) || !oe_save_file(d, a, err, sizeof err))
            return RC_ERROR;
        after_change();
        return 0;
    }
    if (!oe_stricmp(c, "CLOSE")) {
        a = arg(&s);
        if (a && !oe_stricmp(a, "FORCE"))
            oe_doc_mark_saved(d);
        return oe_close_tab(A.cur, 1) ? 0 : RC_WARN;
    }
    if (!oe_stricmp(c, "TAB")) {
        long n = (a = arg(&s)) ? atol(a) : 0;
        if (n < 1 || n > A.ntabs)
            return RC_ERROR;
        oe_switch_tab((int)n - 1);
        return 0;
    }
    if (!oe_stricmp(c, "GOTO")) {
        long line = (a = arg(&s)) ? atol(a) : 0, col = (a = arg(&s)) ? atol(a) : 1;
        if (line < 1)
            return RC_ERROR;
        oe_doc_set_caret(d, oe_doc_pos_at(d, line - 1, (int)(col > 0 ? col - 1 : 0)), 0);
        oe_show_caret();
        oe_draw_all();
        return 0;
    }
    if (!oe_stricmp(c, "FIND")) {
        if (!(a = arg(&s)))
            return RC_ERROR;
        snprintf(A.find_text, sizeof A.find_text, "%s", a);
        return oe_find_next(0, 0) ? 0 : RC_WARN;
    }
    if (!oe_stricmp(c, "REPLACEALL")) {
        char *with;
        long n;
        if (!(a = arg(&s)) || !(with = arg(&s)))
            return RC_ERROR;
        n = oe_replace_all(d, a, 0, with);
        if ((*res = malloc(16)))
            *resn = sprintf(*res, "%ld", n);
        after_change();
        return 0;
    }
    if (!oe_stricmp(c, "INSERT")) {
        while (*s == ' ')
            s++;
        if (*s == '"' || *s == '\'')
            a = arg(&s);
        else
            a = s;
        if (!oe_doc_insert(d, a, (long)strlen(a), 0))
            return RC_ERROR;
        oe_show_caret();
        after_change();
        return 0;
    }
    if (!oe_stricmp(c, "GETTEXT")) {
        long n;
        *res = oe_doc_text(d, 0, oe_doc_len(d) > 65535 ? 65535 : oe_doc_len(d), &n);
        *resn = n;
        return *res ? 0 : RC_ERROR;
    }
    if (!oe_stricmp(c, "GETLINE")) {
        long line = (a = arg(&s)) ? atol(a) - 1 : oe_doc_line(d), st, n;
        if (line < 0 || line >= oe_doc_lines(d))
            return RC_ERROR;
        st = oe_buf_line_start(&d->buf, line);
        *res = oe_doc_text(d, st, st + oe_buf_line_len(&d->buf, line), &n);
        *resn = n;
        return *res ? 0 : RC_ERROR;
    }
    if (!oe_stricmp(c, "GETFILE")) {
        const char *p = d->path[0] ? d->path : d->name;
        if ((*res = malloc(strlen(p) + 1)))
            *resn = sprintf(*res, "%s", p);
        return 0;
    }
    if (!oe_stricmp(c, "GETPOS")) {
        if ((*res = malloc(48)))
            *resn = sprintf(*res, "%ld %d", oe_doc_line(d) + 1, oe_doc_col(d, d->caret) + 1);
        return 0;
    }
    return RC_ERROR;
}

void oe_rexx_news(void)
{
    struct RexxMsg *m;
    if (!A.rexx_port)
        return;
    while ((m = (struct RexxMsg *)GetMsg(A.rexx_port))) {
        char *res = NULL, cmd[512];
        long resn = 0, rc;
        if (!IsRexxMsg(m)) {
            ReplyMsg((struct Message *)m);
            continue;
        }
        snprintf(cmd, sizeof cmd, "%s", (char *)m->rm_Args[0]);
        rc = run(cmd, &res, &resn);
        m->rm_Result1 = rc;
        m->rm_Result2 = 0;
        if (!rc && res && (m->rm_Action & RXFF_RESULT))
            m->rm_Result2 = (LONG)CreateArgstring((STRPTR)res, (ULONG)resn);
        free(res);
        ReplyMsg((struct Message *)m);
    }
}
