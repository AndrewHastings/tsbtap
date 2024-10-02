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
 * TSB file routines.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include "simtap.h"
#include "sink.h"
#include "outfile.h"
#include "tfilefmt.h"
#include "tsbtap.h"



typedef struct {
	tfile_ctx_t	*rec_ctx;
	int		rec_nleft;	/* in bytes */
	int		rec_pad;	/* in bytes */
} rec_ctx_t;


void rec_init(rec_ctx_t *ctx, tfile_ctx_t *tfile, int recsz)
{
	assert(recsz <= 256);
	assert(recsz > 0);

	ctx->rec_ctx = tfile;
	ctx->rec_nleft = 2 * recsz;
	ctx->rec_pad = 512 - 2*recsz;
}


void rec_skip(rec_ctx_t *ctx)
{
	unsigned char buf[512];
	int nskip, nread;

	nskip = ctx->rec_nleft + ctx->rec_pad;
	nread = tfile_getbytes(ctx->rec_ctx, buf, nskip);
	if (nread != nskip)
		dprint(("rec_skip: EOF at 0x%lx\n",
			ftell(ctx->rec_ctx->tf_tap->tp_fp)));
	ctx->rec_nleft = 0;
}


/* returns number of bytes copied, -1 if EOF. nbytes must be even */
int rec_getbytes(rec_ctx_t *ctx, unsigned char *buf, int nbytes)
{
	int nread;

	assert((nbytes & 1) == 0);
	nbytes = MIN(ctx->rec_nleft, nbytes);
	nread = tfile_getbytes(ctx->rec_ctx, buf, nbytes);
	if (nread != nbytes)
		dprint(("rec_getbytes: EOF at 0x%lx\n",
			ftell(ctx->rec_ctx->tf_tap->tp_fp)));
	if (nread < 0)
		return nread;
	ctx->rec_nleft -= nread;
	return nread;
}


char *extract_ascii_file(tfile_ctx_t *tfile, char *fn, char *oname,
			 unsigned char *dbuf)
{
	SINK *snp;
	unsigned char buf[512];
	char *err = NULL;
	int rv = 0;

	if (BE16(dbuf+16) == 0xffff) {
		unsigned device = BE16(dbuf+18);

		printf("%s: not extracting device %c%c%d\n", fn,
		       'A' + (device >> 10), 'A' + ((device >> 5) & 0x1f),
		       device & 0x1f);
		return "";
	}

	snp = out_open(fn, "txt", oname);
	if (!snp)
		return "";

	dprint(("extract_ascii_file: %s\n", fn));

	while (rv >= 0) {
		rec_ctx_t ctx;

		rec_init(&ctx, tfile, 256);

		while ((rv = rec_getbytes(&ctx, buf, 2)) == 2) {
			int stlen, nbytes;

			stlen = BE16(buf);
			dprint(("extract_ascii_file: code %04x\n", stlen));

			/* EOF marker or end-of-record */
			if (stlen == 0xffff) {
				rv = -1;
				break;
			}
			if (stlen == 0xfffe)
				break;

			nbytes = (stlen+1) & ~1;
			if (rec_getbytes(&ctx, buf, nbytes) != nbytes) {
				err = "string extends past end of ASCII file";
				break;
			}

			if (sink_write(snp, buf, stlen) != stlen) {
				perror(oname);
				err = "";
				break;
			}

			sink_putc('\n', snp);
		}

		if (err)
			break;

		rec_skip(&ctx);
	}

	out_close(snp);
	return err;
}


char *extract_basic_file(tfile_ctx_t *tfile, char *fn, char *oname,
			 unsigned char *dbuf)
{
	SINK *snp;
	unsigned char buf[512];
	char *err = NULL;
	int rv = 0;
	int recsz = BE16(dbuf+8);

	snp = out_open(fn, "csv", oname);
	if (!snp)
		return "";

	dprint(("extract_basic_file: %s\n", fn));

	while (rv >= 0) {
		rec_ctx_t ctx;
		char *sep = "";

		rec_init(&ctx, tfile, recsz);

		for ( ; (rv = rec_getbytes(&ctx, buf, 2)) == 2; sep = ",") {
			int code, bits;

			code = BE16(buf);
			dprint(("extract_basic_file: code %04x\n", code));

			/* EOF marker or end-of-record */
			if (code == 0xffff) {
				sink_printf(snp, "%s END", sep);
				break;
			}
			if (code == 0xfffe)
				break;

			/* string */
			if (buf[0] == 0x02) {
				int i, c, stlen = buf[1] & 0xff;

				/* consume even number of bytes */
				bits = (stlen+1) & ~1;
				if (rec_getbytes(&ctx, buf, bits) != bits) {
					err = "string extends past end of "
					      "record";
					break;
				}

				sink_printf(snp, "%s\"", sep);
				for (i = 0; i < stlen; i++) {
					switch (c = buf[i]) {
					    case '"':
						sink_printf(snp, "\"\"");
						break;

					    case '\0':
						sink_printf(snp, "\\000");
						break;

					    case '\n':
						sink_printf(snp, "\\n");
						break;

					    default:
						sink_putc(c, snp);
						break;
					}
				}
				sink_putc('"', snp);
				continue;
			}

			/* number */
			bits = code & 0xc000;
			if (bits != 0x8000 && bits != 0x4000 && code != 0) {
				printf("unrecognized item 0x%04x\n", code);
				err = "";
				break;
			}
			if (rec_getbytes(&ctx, buf+2, 2) != 2) {
				err = "number extends past end of record";
				break;
			}
			sink_printf(snp, "%s", sep);
			print_number(snp, buf);
		}

		if (err)
			break;

		rec_skip(&ctx);
		if (rv >= 0)
			sink_putc('\n', snp);
	}

	out_close(snp);
	return err;
}
