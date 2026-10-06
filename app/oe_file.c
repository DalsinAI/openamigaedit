/* oe_file: reading and saving files, the file requester, the clipboard and
 * the colour kinds. Everything here is in AmigaOS 3.2 itself: dos, asl and
 * iffparse.
 *
 * Saving never leaves a half-written file: the text goes to a new file
 * beside the old one, and only when that is complete does it take the old
 * one's name. Files in S: and DEVS: keep the previous version as .bak.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <libraries/asl.h>
#include <libraries/iffparse.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/asl.h>
#include <proto/iffparse.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oe_app.h"
#include "oe_pat.h"

struct Library *AslBase;
struct Library *IFFParseBase;

#define MAX_FILE (32L * 1024 * 1024)

int oe_load_file(oe_doc *d, const char *path)
{
    long n;
    char *t = oe_load_text(path, MAX_FILE, &n);
    int ok;
    if (!t)
        return 0;
    ok = oe_doc_load(d, t, n);
    free(t);
    if (ok) {
        snprintf(d->path, sizeof d->path, "%s", path);
        oe_pick_kind(d);
    }
    return ok;
}

static int keeps_backup(const char *path)
{
    static const char *const dirs[] = { "S:", "DEVS:", "SYS:S/", "SYS:Devs/" };
    int i;
    for (i = 0; i < 4; i++)
        if (oe_strnicmp(path, dirs[i], (long)strlen(dirs[i])) == 0)
            return 1;
    return 0;
}

static void fault(char *err, int errlen, const char *what)
{
    char why[80];
    Fault(IoErr(), NULL, (STRPTR)why, sizeof why);
    snprintf(err, errlen, "%s: %s", what, why);
}

int oe_save_file(oe_doc *d, const char *path, char *err, int errlen)
{
    char tmp[OE_PATH_MAX + 8], bak[OE_PATH_MAX + 8];
    long n = oe_doc_save_size(d);
    char *bytes = malloc(n + 1);
    BPTR f, old;
    LONG prot = -1;
    char comment[80] = "";
    int ok = 0;

    if (!bytes) {
        snprintf(err, errlen, "Not enough memory to save");
        return 0;
    }
    oe_doc_save_bytes(d, bytes);
    snprintf(tmp, sizeof tmp, "%s.oe-new", path);
    snprintf(bak, sizeof bak, "%s.bak", path);

    /* What the old file had, to keep. */
    if ((old = Lock((CONST_STRPTR)path, SHARED_LOCK))) {
        struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
        if (fib) {
            if (Examine(old, fib)) {
                prot = fib->fib_Protection;
                snprintf(comment, sizeof comment, "%s", (char *)fib->fib_Comment);
            }
            FreeDosObject(DOS_FIB, fib);
        }
        UnLock(old);
    }

    if (!(f = Open((CONST_STRPTR)tmp, MODE_NEWFILE))) {
        fault(err, errlen, "Couldn't write the new file");
        free(bytes);
        return 0;
    }
    if (Write(f, bytes, n) != n) {
        fault(err, errlen, "Couldn't write all of the file");
        Close(f);
        DeleteFile((CONST_STRPTR)tmp);
        free(bytes);
        return 0;
    }
    free(bytes);
    if (!Close(f)) {
        fault(err, errlen, "Couldn't finish the file");
        DeleteFile((CONST_STRPTR)tmp);
        return 0;
    }
    /* The new file is complete: now it takes the name. */
    if (prot >= 0) {
        if (keeps_backup(path)) {
            DeleteFile((CONST_STRPTR)bak);
            if (!Rename((CONST_STRPTR)path, (CONST_STRPTR)bak)) {
                fault(err, errlen, "Couldn't keep the old version as .bak");
                DeleteFile((CONST_STRPTR)tmp);
                return 0;
            }
        } else if (!DeleteFile((CONST_STRPTR)path)) {
            /* Protected from deleting: say so, keep the old file. */
            fault(err, errlen, "Couldn't replace the old file");
            DeleteFile((CONST_STRPTR)tmp);
            return 0;
        }
    }
    if (!Rename((CONST_STRPTR)tmp, (CONST_STRPTR)path)) {
        fault(err, errlen, "The text is saved as .oe-new, but couldn't take the file's name");
        return 0;
    }
    if (prot >= 0) {
        SetProtection((CONST_STRPTR)path, prot);
        if (comment[0])
            SetComment((CONST_STRPTR)path, (CONST_STRPTR)comment);
    }
    ok = 1;
    if (strcmp(d->path, path) != 0) {
        snprintf(d->path, sizeof d->path, "%s", path);
        oe_pick_kind(d);
    }
    oe_doc_mark_saved(d);
    return ok;
}

int oe_ask_file(int save, const char *title, char *path, int size)
{
    struct FileRequester *fr;
    char drawer[OE_PATH_MAX], file[108];
    int ok = 0;
    if (!AslBase && !(AslBase = OpenLibrary((CONST_STRPTR)"asl.library", 38)))
        return 0;
    drawer[0] = file[0] = 0;
    if (path[0]) {
        const char *fp = (const char *)FilePart((STRPTR)path);
        int dn = (int)(fp - path);
        if (dn >= (int)sizeof drawer)
            dn = sizeof drawer - 1;
        memcpy(drawer, path, dn);
        drawer[dn] = 0;
        snprintf(file, sizeof file, "%s", fp);
    }
    fr = AllocAslRequestTags(ASL_FileRequest, ASLFR_Window, (ULONG)A.win, ASLFR_SleepWindow, TRUE,
                             ASLFR_TitleText, (ULONG)title, ASLFR_InitialDrawer, (ULONG)drawer,
                             ASLFR_InitialFile, (ULONG)file, ASLFR_DoSaveMode, save, ASLFR_RejectIcons, TRUE,
                             TAG_DONE);
    if (!fr)
        return 0;
    if (AslRequest(fr, NULL) && fr->fr_File && fr->fr_File[0]) {
        snprintf(path, size, "%s", (char *)fr->fr_Drawer);
        ok = AddPart((STRPTR)path, fr->fr_File, size) != 0;
    }
    FreeAslRequest(fr);
    return ok;
}

/* ---- the clipboard: IFF FTXT on unit 0, text in ISO-8859-1 ---- */

#define ID_FTXT MAKE_ID('F', 'T', 'X', 'T')
#define ID_CHRS MAKE_ID('C', 'H', 'R', 'S')

static int open_iff(void)
{
    return IFFParseBase || (IFFParseBase = OpenLibrary((CONST_STRPTR)"iffparse.library", 39)) != NULL;
}

int oe_clip_write(const char *s, long n)
{
    struct IFFHandle *iff;
    int ok = 0;
    if (!open_iff() || !(iff = AllocIFF()))
        return 0;
    if ((iff->iff_Stream = (ULONG)OpenClipboard(0))) {
        InitIFFasClip(iff);
        if (!OpenIFF(iff, IFFF_WRITE)) {
            if (!PushChunk(iff, ID_FTXT, ID_FORM, IFFSIZE_UNKNOWN) && !PushChunk(iff, 0, ID_CHRS, IFFSIZE_UNKNOWN)) {
                ok = WriteChunkBytes(iff, (APTR)s, n) == n;
                PopChunk(iff);
                PopChunk(iff);
            }
            CloseIFF(iff);
        }
        CloseClipboard((struct ClipboardHandle *)iff->iff_Stream);
    }
    FreeIFF(iff);
    return ok;
}

char *oe_clip_read(long *n)
{
    struct IFFHandle *iff;
    char *t = NULL;
    *n = 0;
    if (!open_iff() || !(iff = AllocIFF()))
        return NULL;
    if ((iff->iff_Stream = (ULONG)OpenClipboard(0))) {
        InitIFFasClip(iff);
        if (!OpenIFF(iff, IFFF_READ)) {
            if (!StopChunk(iff, ID_FTXT, ID_CHRS)) {
                while (!ParseIFF(iff, IFFPARSE_SCAN)) {
                    struct ContextNode *cn = CurrentChunk(iff);
                    char *nt;
                    if (!cn || cn->cn_Size <= 0)
                        continue;
                    if (!(nt = realloc(t, *n + cn->cn_Size + 1)))
                        break;
                    t = nt;
                    *n += ReadChunkBytes(iff, t + *n, cn->cn_Size);
                    t[*n] = 0;
                }
            }
            CloseIFF(iff);
        }
        CloseClipboard((struct ClipboardHandle *)iff->iff_Stream);
    }
    FreeIFF(iff);
    return t;
}

/* ---- colour kinds ---- */

static int kind_cmp(const void *a, const void *b)
{
    return oe_stricmp(((const oe_kind *)a)->name, ((const oe_kind *)b)->name);
}

static void load_kinds_from(const char *dir)
{
    BPTR l = Lock((CONST_STRPTR)dir, SHARED_LOCK);
    struct FileInfoBlock *fib;
    if (!l)
        return;
    if ((fib = AllocDosObject(DOS_FIB, NULL))) {
        if (Examine(l, fib))
            while (A.nkinds < MAX_KINDS && ExNext(l, fib)) {
                char path[OE_PATH_MAX], err[96];
                const char *name = (const char *)fib->fib_FileName;
                int nl = (int)strlen(name);
                long n;
                char *t;
                if (fib->fib_DirEntryType >= 0 || nl < 6 || oe_stricmp(name + nl - 5, ".kind") != 0)
                    continue;
                snprintf(path, sizeof path, "%s", dir);
                AddPart((STRPTR)path, (STRPTR)name, sizeof path);
                if ((t = oe_load_text(path, 65536, &n))) {
                    if (oe_kind_parse(&A.kinds[A.nkinds], t, err, sizeof err))
                        A.nkinds++;
                    free(t);
                }
            }
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(l);
}

void oe_load_kinds(void)
{
    load_kinds_from("ENVARC:OpenEdit/Kinds");
    if (!A.nkinds)
        load_kinds_from("PROGDIR:Kinds");
    if (A.nkinds > 1)
        qsort(A.kinds, A.nkinds, sizeof A.kinds[0], kind_cmp);
}

void oe_pick_kind(oe_doc *d)
{
    char first[96];
    long n = oe_buf_copy(&d->buf, 0, sizeof first - 1, first);
    d->kind = oe_kind_pick(A.kinds, A.nkinds, d->path, first, (int)n);
    d->lsok = 0;
}
