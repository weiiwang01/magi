// SPDX-License-Identifier: LGPL-2.1+
/*
 * Tiny printf version for SPL
 *
 * Copied from:
 * http://www.sparetimelabs.com/printfrevisited/printfrevisited.php
 *
 * Copyright (C) 2004,2008  Kustaa Nyholm
 */

#include <stdarg.h>
#include <stdbool.h>
#include <ctype.h>
#include <inttypes.h>

#include "tiny_printf.h"

static void out(struct tiny_printf_info *info, char c)
{
	*info->bf++ = c;
}

static void out_dgt(struct tiny_printf_info *info, char dgt)
{
	out(info, dgt + (dgt < 10 ? '0' : 'a' - 10));
	info->zs = 1;
}

static void div_out(struct tiny_printf_info *info, unsigned long *num,
		    unsigned long div)
{
	unsigned char dgt = 0;

	while (*num >= div) {
		*num -= div;
		dgt++;
	}

	if (info->zs || dgt > 0)
		out_dgt(info, dgt);
}

static void pointer(struct tiny_printf_info *info, const char *fmt, void *ptr)
{
	unsigned long num = (uintptr_t)ptr;
	unsigned long div;

	switch (*fmt) {
	case 'a':
		switch (fmt[1]) {
		case 'p':
		default:
			num = *(uintptr_t *)ptr;
			break;
		}
		break;
	case 'm':
	case 'M':
	case 'I':
		out(info, '?');
		return;
	default:
		break;
	}

	div = 1UL << (sizeof(long) * 8 - 4);
	for (; div; div /= 0x10)
		div_out(info, &num, div);
}

int tiny_vprintf(struct tiny_printf_info *info, const char *fmt, va_list va)
{
	char ch;
	char *p;
	unsigned long num;
	char buf[12];
	unsigned long div;

	while ((ch = *(fmt++))) {
		if (ch != '%') {
			info->putc(info, ch);
		} else {
			bool lz = false;
			int width = 0;
			bool islong = false;
			bool force_char = false;

			ch = *(fmt++);
			if (ch == '-')
				ch = *(fmt++);

			if (ch == '0') {
				ch = *(fmt++);
				lz = 1;
			}

			if (ch >= '0' && ch <= '9') {
				width = 0;
				while (ch >= '0' && ch <= '9') {
					width = (width * 10) + ch - '0';
					ch = *fmt++;
				}
			}
			if (ch == 'l') {
				ch = *(fmt++);
				islong = true;
			}

			info->bf = buf;
			p = info->bf;
			info->zs = 0;

			switch (ch) {
			case '\0':
				goto abort;
			case 'u':
			case 'd':
			case 'i':
				div = 1000000000;
				if (islong) {
					num = va_arg(va, unsigned long);
					if (sizeof(long) > 4)
						div *= div * 10;
				} else {
					num = va_arg(va, unsigned int);
				}

				if (ch != 'u') {
					if (islong && (long)num < 0) {
						num = -(long)num;
						out(info, '-');
					} else if (!islong && (int)num < 0) {
						num = -(int)num;
						out(info, '-');
					}
				}
				if (!num) {
					out_dgt(info, 0);
				} else {
					for (; div; div /= 10)
						div_out(info, &num, div);
				}
				break;
			case 'p':
				pointer(info, fmt, va_arg(va, void *));
				/*
				 * Skip this because it pulls in _ctype which is
				 * 256 bytes, and we don't generally implement
				 * pointer anyway
				 */
				while (isalnum((unsigned char)(fmt[0])))
					fmt++;
				break;
			case 'x':
			case 'X':
				if (islong) {
					num = va_arg(va, unsigned long);
					div = 1UL << (sizeof(long) * 8 - 4);
				} else {
					num = va_arg(va, unsigned int);
					div = 0x10000000;
				}
				if (!num) {
					out_dgt(info, 0);
				} else {
					for (; div; div /= 0x10)
						div_out(info, &num, div);
				}
				break;
			case 'c':
				out(info, (char)(va_arg(va, int)));
				/* For the case when it's \0 char */
				force_char = true;
				break;
			case 's':
				p = va_arg(va, char*);
				break;
			case '%':
				out(info, '%');
				break;
			default:
				out(info, '?');
				break;
			}

			*info->bf = 0;
			info->bf = p;
			while (width > 0 && info->bf && *info->bf++)
				width--;
			while (width-- > 0)
				info->putc(info, lz ? '0' : ' ');
			if (p) {
				while ((ch = *p++) || force_char) {
					info->putc(info, ch);
					force_char = false;
				}
			}
		}
	}

abort:
	return 0;
}
