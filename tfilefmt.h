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
 * Routines for reading tapes containing files delimited by tapemarks.
 */

#ifndef _TFILEFMT_H
#define _TFILEFMT_H 1

#include "simtap.h"

typedef struct {
	TAPE	*tfile_tap;
	char	*tfile_buf;
	char	*tfile_bp;
	int	tfile_nleft;
	int	tfile_ateof;
} tfile_ctx_t;

extern void tfile_ctx_init(tfile_ctx_t *ctx, TAPE *tap, char *buf, int nbytes);
extern void tfile_ctx_fini(tfile_ctx_t *ctx);
extern int tfile_getbytes(tfile_ctx_t *ctx, char *buf, int nbytes);
extern int tfile_skipbytes(tfile_ctx_t *ctx, int nbytes);
extern int tfile_skipf(tfile_ctx_t *ctx);

#endif /* _TFILEFMT_H */
