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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "simtap.h"
#include "tfilefmt.h"
#include "tsbtap.h"


/* reading if buf != NULL, else writing */
/* returns -1 on error */
int tfile_ctx_init(tfile_ctx_t *ctx, TAPE *tap, char *buf, int nbytes, int hdr)
{
	memset(ctx, 0, sizeof(tfile_ctx_t));
	ctx->tf_tap = tap;
	ctx->tf_hdr = hdr;
	ctx->tf_bufsize = nbytes;

	if (buf) {
		if (tap_is_write(tap)) {
			fprintf(stderr, "tfile_ctx_init: attempt to read "
					"tape open for writing\n");
			return -1;
		}
		if (nbytes < hdr) {
			ctx->tf_buf = ctx->tf_bp = NULL;
			ctx->tf_nleft = 0;
		} else {
			ctx->tf_buf = ctx->tf_bp = buf + hdr;
			ctx->tf_nleft = nbytes - hdr;
		}
	} else {
		if (!tap_is_write(tap)) {
			fprintf(stderr, "tfile_ctx_init: attempt to write "
					"tape open for reading\n");
			return -1;
		}
		ctx->tf_buf = malloc(nbytes + hdr);
		if (!ctx->tf_buf) {
			fprintf(stderr, "tfile_ctx_init: out of memory for "
					"writing\n");
			return -1;
		}
		ctx->tf_bp = ctx->tf_buf + hdr;
		ctx->tf_nleft = nbytes;
	}
	return 0;
}


void tfile_ctx_fini(tfile_ctx_t *ctx)
{
	if (tap_is_write(ctx->tf_tap)) {
		if (ctx->tf_bp - ctx->tf_buf > ctx->tf_hdr)
			fprintf(stderr, "tfile_ctx_fini: %ld unwritten bytes\n",
					ctx->tf_bp - ctx->tf_buf);
		free(ctx->tf_buf);
	} else {
		if (ctx->tf_nleft)
			dprint(("tfile_ctx_fini: nleft=%d\n",
				ctx->tf_nleft));
	}
	memset(ctx, 0, sizeof(tfile_ctx_t));
}


/* skip over tape blocks until tapemark */
/* returns -1 if error, else 0 */
int tfile_skipf(tfile_ctx_t *ctx)
{
	ssize_t nbytes;
	char *unused;

	dprint(("tfile_skipf\n"));

	if (tap_is_write(ctx->tf_tap)) {
		fprintf(stderr, "tfile_skipf: attempt to read tape open "
				"for writing\n");
		return -1;
	}

	if (!ctx->tf_ateof) {
		while (nbytes = tap_readblock(ctx->tf_tap, &unused)) {
			if (nbytes == -2)
				return -1;
			if (nbytes < 0)
				break;
		}
	}
	ctx->tf_buf = ctx->tf_bp = NULL;
	ctx->tf_nleft = 0;
	ctx->tf_ateof = 1;

	return 0;
}


/* returns number of bytes skipped, -2 if error, -1 if end of file */
int tfile_skipbytes(tfile_ctx_t *ctx, int nbytes)
{
	ssize_t nread;
	int rv = 0;

	dprint(("tfile_skipbytes: skip %d bytes\n", nbytes));

	if (tap_is_write(ctx->tf_tap)) {
		fprintf(stderr, "tfile_skipbytes: attempt to read tape open "
				"for writing\n");
		return -1;
	}

	if (ctx->tf_ateof)
		return -1;

	while (ctx->tf_nleft < nbytes) {
		dprint(("tfile_skipbytes: nleft=%d, refill\n",
			ctx->tf_nleft));

		rv += ctx->tf_nleft;
		nbytes -= ctx->tf_nleft;

		/* read next tape block */
		nread = tap_readblock(ctx->tf_tap, &ctx->tf_buf);
		dprint(("tfile_skipbytes: readblock returned %ld\n", nread));
		if (nread <= 0) {
			ctx->tf_bp = NULL;
			ctx->tf_nleft = 0;
			ctx->tf_ateof = 1;
			return nread == -2 ? -2 : -1;
		}

		/* skip over pre-Access header bytes */
		ctx->tf_buf += ctx->tf_hdr;
		nread -= ctx->tf_hdr;
		if (nread <= 0) {
			ctx->tf_nleft = 0;
			continue;
		}

		ctx->tf_bp = ctx->tf_buf;
		ctx->tf_nleft = nread;
	}

	ctx->tf_bp += nbytes;
	ctx->tf_nleft -= nbytes;
	rv += nbytes;
	return rv;
}


/* returns number of bytes copied, -2 if error, -1 if end of file */
int tfile_getbytes(tfile_ctx_t *ctx, char *buf, int nbytes)
{
	int nread, rv = 0;

	if (tap_is_write(ctx->tf_tap)) {
		fprintf(stderr, "tfile_getbytes: attempt to read tape open "
				"for writing\n");
		return -1;
	}

	if (ctx->tf_ateof)
		return -1;

	while (nbytes > 0) {
		if (ctx->tf_nleft == 0) {
			/* read next tape block */
			nread = tap_readblock(ctx->tf_tap, &ctx->tf_buf);
			dprint(("tfile_getbytes: readblock returned %d\n",
				nread));
			if (nread <= 0) {
				ctx->tf_bp = NULL;
				ctx->tf_nleft = 0;
				ctx->tf_ateof = 1;
				if (nread == -2)
					return -2;
				dprint(("tfile_getbytes: short copy %d\n", rv));
				return rv ? rv : -1;
			}

			/* skip over pre-Access header bytes */
			ctx->tf_buf += ctx->tf_hdr;
			nread -= ctx->tf_hdr;
			if (nread <= 0) {
				ctx->tf_nleft = 0;
				continue;
			}

			ctx->tf_bp = ctx->tf_buf;
			ctx->tf_nleft = nread;
		}

		nread = MIN(nbytes, ctx->tf_nleft);
		memcpy(buf, ctx->tf_bp, nread);

		ctx->tf_bp += nread;
		ctx->tf_nleft -= nread;
		buf += nread;
		nbytes -= nread;
		rv += nread;
	}

	dprint(("tfile_getbytes: rv=%d\n", rv));
	return rv;
}


/* returns 0 on success, -1 if error */
int tfile_flushblock(tfile_ctx_t *ctx, int minsz)
{
	int n;

	if (!tap_is_write(ctx->tf_tap)) {
		fprintf(stderr, "tfile_flushblock: attempt to write tape open "
				"for reading\n");
		return -1;
	}

	n = ctx->tf_bp - ctx->tf_buf - ctx->tf_hdr;
	if (n > 0) {
		if (n < minsz)
			n = minsz;
		if (ctx->tf_hdr) {
			ctx->tf_buf[0] = (-n/2) >> 8;
			ctx->tf_buf[1] = (-n/2) & 0xff;
		}
		if (tap_writeblock(ctx->tf_tap, ctx->tf_buf, n+ctx->tf_hdr) < 0)
			return -1;
	}

	ctx->tf_bp = ctx->tf_buf + ctx->tf_hdr;
	ctx->tf_nleft = TBLOCKSIZE;
	return 0;
}


int tfile_putbytes(tfile_ctx_t *ctx, char *buf, int nbytes)
{
	int n, rv = 0;

	if (!tap_is_write(ctx->tf_tap)) {
		fprintf(stderr, "tfile_putbytes: attempt to write tape open "
				"for reading\n");
		return -1;
	}

	for ( ; nbytes > 0; nbytes -= n) {
		n = MIN(ctx->tf_nleft, nbytes);
		memcpy(ctx->tf_bp, buf, n);
		rv += n;
		buf += n;
		ctx->tf_bp += n;
		ctx->tf_nleft -= n;
		if (ctx->tf_nleft == 0) {
			if (tfile_flushblock(ctx, 0) < 0)
				return -1;
		}
	}

	return rv;
}


int tfile_writef(tfile_ctx_t *ctx, int minsz)
{
	if (!tap_is_write(ctx->tf_tap)) {
		fprintf(stderr, "tfile_writef: attempt to write tape open "
				"for reading\n");
		return -1;
	}

	/* flush block and write tapemark */
	if (tfile_flushblock(ctx, minsz) < 0)
		return -1;
	if (tap_writeblock(ctx->tf_tap, NULL, 0) < 0)
		return -1;
	ctx->tf_nleft = ctx->tf_bufsize;

	return 0;
}
