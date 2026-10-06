/* OpenEdit: run main on a stack of our own size.
 * MIT, Copyright (c) 2026 Dalsin Limited.
 *
 * Run and a Shell give a program the Shell's stack (4 KB by default), and
 * libnix ignores __stack. The same helper as OpenFiles, which overran a
 * 4 KB stack. */
#include "oe_stack.h"
#include <exec/types.h>
#include <exec/tasks.h>
#include <exec/memory.h>
#include <proto/exec.h>

/* Static, not on the stack: nothing may be read from the old stack's frame
 * between the two StackSwap calls. */
static struct StackSwapStruct swap;
static int (*volatile swap_body)(void);
static volatile int swap_rc;

/* The call sits in a function of its own, so all of it happens on the new stack. */
static void __attribute__((noinline)) run_body(void)
{
    swap_rc = swap_body();
}

int oe_main_with_stack(int (*body)(void), unsigned long bytes)
{
    struct Task *me = FindTask(NULL);
    unsigned long have = (unsigned long)me->tc_SPUpper - (unsigned long)me->tc_SPLower;
    APTR lower;

    if (have >= bytes || !(lower = AllocVec(bytes, MEMF_ANY)))
        return body();
    swap.stk_Lower = lower;
    swap.stk_Upper = (ULONG)lower + bytes;
    swap.stk_Pointer = (APTR)swap.stk_Upper;
    swap_body = body;
    StackSwap(&swap);
    run_body();
    StackSwap(&swap);
    FreeVec(lower);
    return swap_rc;
}
