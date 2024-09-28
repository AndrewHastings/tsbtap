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
 * Routines for reading/writing tapes containing files delimited by tapemarks.
 */

#ifndef _TFILEFMT_H
#define _TFILEFMT_H 1

#include "simtap.h"

#define TBLOCKSIZE 2048		/* observed tape block size */

typedef struct {
	TAPE	*tf_tap;
	char	*tf_buf;
	char	*tf_bp;
	int	tf_hdr;
	int	tf_nleft;
	int	tf_bufsize;
	int	tf_ateof;
} tfile_ctx_t;

extern int tfile_ctx_init(tfile_ctx_t *ctx, TAPE *tap, char *buf, int nbytes,
			  int hdr);
extern void tfile_ctx_fini(tfile_ctx_t *ctx);
extern int tfile_getbytes(tfile_ctx_t *ctx, char *buf, int nbytes);
extern int tfile_skipbytes(tfile_ctx_t *ctx, int nbytes);
extern int tfile_skipf(tfile_ctx_t *ctx);
extern int tfile_putbytes(tfile_ctx_t *ctx, char *buf, int nbytes);
extern int tfile_writef(tfile_ctx_t *ctx, int minsz);

#endif /* _TFILEFMT_H */
