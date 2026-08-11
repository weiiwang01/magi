// SPDX-License-Identifier: LGPL-2.1+
/*
 * Tiny printf version for SPL
 *
 * Copied from:
 * http://www.sparetimelabs.com/printfrevisited/printfrevisited.php
 *
 * Copyright (C) 2004,2008  Kustaa Nyholm
 */

#ifndef PRINTF_H
#define PRINTF_H

#include <stdarg.h>

struct tiny_printf_info {
    char *bf;	/* Digit buffer */
    char zs;	/* non-zero if a digit has been written */

    /* Output a character */
    void (*putc)(struct tiny_printf_info *info, char ch);
};

int tiny_vprintf(struct tiny_printf_info *info, const char *fmt, va_list va);

#endif //PRINTF_H
