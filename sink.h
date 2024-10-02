/*
 * Copyright 2024 Andrew B. Hastings. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*
 * Routines for using a string or a file as an output sink.
 */

#ifndef _SINK_H
#define _SINK_H 1

typedef struct {
	FILE	*snk_fp;	/* file */
	int	snk_nwrite;	/* file */
	char	*snk_buf;	/* string */
	char	*snk_bp;	/* string */
	int	snk_nleft;	/* string */
} SINK;

static inline FILE *sink_getf(SINK *snp) { return snp->snk_fp; }

extern SINK *sink_initf(FILE *fp);
extern SINK *sink_initstr(char *buf, int nbytes);
extern int sink_printf(SINK *snp, char *fmt, ...);
#pragma printflike sink_printf
extern int sink_write(SINK *snp, char *buf, int nbytes);
extern int sink_putc(int c, SINK *snp);
extern int sink_fini(SINK *snp);

#endif /* _SINK_H */
