/*
 * Copyright 2024 Andrew B. Hastings. All rights reserved.
 *
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
#include "outfile.h"
#include "simtap.h"
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
			ftell(ctx->rec_ctx->tfile_tap->tp_fp)));
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
			ftell(ctx->rec_ctx->tfile_tap->tp_fp)));
	if (nread < 0)
		return nread;
	ctx->rec_nleft -= nread;
	return nread;
}


char *extract_ascii_file(tfile_ctx_t *tfile, char *fn, char *oname,
			 unsigned char *dbuf)
{
	FILE *fp;
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

	fp = out_open(fn, "txt", oname);
	if (!fp)
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

			if (fwrite(buf, 1, stlen, fp) != stlen) {
				perror(oname);
				err = "";
				break;
			}

			putc('\n', fp);
		}

		if (err)
			break;

		rec_skip(&ctx);
	}

	out_close(fp);
	return err;
}


char *extract_basic_file(tfile_ctx_t *tfile, char *fn, char *oname,
			 unsigned char *dbuf)
{
	FILE *fp;
	unsigned char buf[512];
	char *err = NULL;
	int rv = 0;
	int recsz = BE16(dbuf+8);

	fp = out_open(fn, "csv", oname);
	if (!fp)
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
				fprintf(fp, "%s END", sep);
				break;
			}
			if (code == 0xfffe)
				break;

			/* string */
			if (buf[0] == 0x02) {
				int stlen = buf[1] & 0xff;

				/* consume even number of bytes */
				bits = (stlen+1) & ~1;
				if (rec_getbytes(&ctx, buf, bits) != bits) {
					err = "string extends past end of record";
					break;
				}
				buf[stlen] = '\0';

				/* TBD: handle embedded quote, null, newline */
				fprintf(fp, "%s\"%s\"", sep, buf);
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
			fprintf(fp, "%s", sep);
			print_number(fp, buf);
		}

		if (err)
			break;

		rec_skip(&ctx);
		if (rv >= 0)
			putc('\n', fp);
	}

	out_close(fp);
	return err;
}
