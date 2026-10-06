/* OpenEdit: run main on a stack of our own size. MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OE_STACK_H
#define OE_STACK_H
/* Runs body on a new stack of `bytes` when the task's own is smaller. */
int oe_main_with_stack(int (*body)(void), unsigned long bytes);
#endif
