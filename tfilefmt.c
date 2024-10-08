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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "simtap.h"
#include "tfilefmt.h"
#include "tsbtap.h"


void tfile_ctx_init(tfile_ctx_t *ctx, TAPE *tap, char *buf, int nbytes, int hdr)
{
	ctx->tfile_tap = tap;
	ctx->tfile_hdr = hdr;
	if (nbytes < hdr) {
		ctx->tfile_buf = ctx->tfile_bp = NULL;
		ctx->tfile_nleft = 0;
	} else {
		ctx->tfile_buf = ctx->tfile_bp = buf + hdr;
		ctx->tfile_nleft = nbytes - hdr;
	}
	ctx->tfile_ateof = 0;
}


void tfile_ctx_fini(tfile_ctx_t *ctx)
{
	if (ctx->tfile_nleft)
		dprint(("tfile_ctx_fini: nleft=%d\n", ctx->tfile_nleft));
	memset(ctx, 0, sizeof(tfile_ctx_t));
}


/* skip over tape blocks until tapemark */
/* returns -1 if error, else 0 */
int tfile_skipf(tfile_ctx_t *ctx)
{
	ssize_t nbytes;
	char *unused;

	dprint(("tfile_skipf\n"));
	if (!ctx->tfile_ateof) {
		while (nbytes = tap_readblock(ctx->tfile_tap, &unused)) {
			if (nbytes == -2)
				return -1;
			if (nbytes < 0)
				break;
		}
	}
	ctx->tfile_buf = ctx->tfile_bp = NULL;
	ctx->tfile_nleft = 0;
	ctx->tfile_ateof = 1;

	return 0;
}


/* returns number of bytes skipped, -2 if error, -1 if end of file */
int tfile_skipbytes(tfile_ctx_t *ctx, int nbytes)
{
	ssize_t nread;
	int rv = 0;

	dprint(("tfile_skipbytes: skip %d bytes\n", nbytes));

	if (ctx->tfile_ateof)
		return -1;

	while (ctx->tfile_nleft < nbytes) {
		dprint(("tfile_skipbytes: nleft=%d, refill\n",
			ctx->tfile_nleft));

		rv += ctx->tfile_nleft;
		nbytes -= ctx->tfile_nleft;

		/* read next tape block */
		nread = tap_readblock(ctx->tfile_tap, &ctx->tfile_buf);
		dprint(("tfile_skipbytes: readblock returned %ld\n", nread));
		if (nread <= 0) {
			ctx->tfile_bp = NULL;
			ctx->tfile_nleft = 0;
			ctx->tfile_ateof = 1;
			return nread == -2 ? -2 : -1;
		}

		/* skip over pre-Access header bytes */
		ctx->tfile_buf += ctx->tfile_hdr;
		nread -= ctx->tfile_hdr;
		if (nread <= 0) {
			ctx->tfile_nleft = 0;
			continue;
		}

		ctx->tfile_bp = ctx->tfile_buf;
		ctx->tfile_nleft = nread;
	}

	ctx->tfile_bp += nbytes;
	ctx->tfile_nleft -= nbytes;
	rv += nbytes;
	return rv;
}


/* returns number of bytes copied, -2 if error, -1 if end of file */
int tfile_getbytes(tfile_ctx_t *ctx, char *buf, int nbytes)
{
	int nread, rv = 0;

	if (ctx->tfile_ateof)
		return -1;

	while (nbytes > 0) {
		if (ctx->tfile_nleft == 0) {
			/* read next tape block */
			nread = tap_readblock(ctx->tfile_tap, &ctx->tfile_buf);
			dprint(("tfile_getbytes: readblock returned %d\n",
				nread));
			if (nread <= 0) {
				ctx->tfile_bp = NULL;
				ctx->tfile_nleft = 0;
				ctx->tfile_ateof = 1;
				if (nread == -2)
					return -2;
				dprint(("tfile_getbytes: short copy %d\n", rv));
				return rv ? rv : -1;
			}

			/* skip over pre-Access header bytes */
			ctx->tfile_buf += ctx->tfile_hdr;
			nread -= ctx->tfile_hdr;
			if (nread <= 0) {
				ctx->tfile_nleft = 0;
				continue;
			}

			ctx->tfile_bp = ctx->tfile_buf;
			ctx->tfile_nleft = nread;
		}

		nread = MIN(nbytes, ctx->tfile_nleft);
		memcpy(buf, ctx->tfile_bp, nread);

		ctx->tfile_bp += nread;
		ctx->tfile_nleft -= nread;
		buf += nread;
		nbytes -= nread;
		rv += nread;
	}

	dprint(("tfile_getbytes: rv=%d\n", rv));
	return rv;
}
