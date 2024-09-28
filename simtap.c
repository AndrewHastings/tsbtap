/*
 * Copyright 2024 Andrew Hastings. All rights reserved.
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
 * Routines for reading/writing SIMH-format tape images.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "tsbtap.h"
#include "simtap.h"

#define LE32(bp)	(((bp)[3] << 24) | ((bp)[2] << 16) | \
			 ((bp)[1] << 8)  | (bp)[0])

/* tp_status bits */
#define	TP_WRITE	0x1
#define	TP_ERR		0x40
#define	TP_EOM		0x80


TAPE *tap_open(char *path, int is_write)
{
	FILE *fp;
	TAPE *rv;
	char *mode = is_write ? "w" : "r";

	fp = fopen(path, mode);
	if (!fp)
		return NULL;

	rv = (TAPE *)malloc(sizeof(TAPE));
	if (rv) {
		rv->tp_fp = fp;
		rv->tp_path = path;
		rv->tp_buf = NULL;
		rv->tp_nbytes = 0;
		rv->tp_status = is_write ? TP_WRITE : 0;
	}

	return rv;
}


void tap_close(TAPE *tap)
{
	fclose(tap->tp_fp);
	if (tap->tp_buf)
		free(tap->tp_buf);
	memset(tap, 0, sizeof(TAPE));
	free(tap);
}


int tap_is_write(TAPE *tap)
{
	return tap->tp_status & TP_WRITE;
}


/* Read next tape block */
/* returns block size, -1=end of tape, -2=error, e.g. out of memory */
ssize_t tap_readblock(TAPE *tap, char **bufp)
{
	unsigned char sbuf[4];
	size_t rv;

	*bufp = NULL;

	if (tap->tp_status & TP_WRITE) {
		fprintf(stderr,
			"%s: tap_readblock not allowed while writing tape",
			tap->tp_path);
		return -2;
	}

	if (tap->tp_status & TP_ERR)
		return -2;

	if (tap->tp_status & TP_EOM)
		return -1;

	/* read header */
	rv = fread(sbuf, 1, 4, tap->tp_fp);
	if (rv < 4) {
		dprint(("%s: tap_readblock: EOF reading header at 0x%lx\n",
			tap->tp_path, ftell(tap->tp_fp) - rv));
		tap->tp_status |= TP_EOM;
		return -1;
	}
	tap->tp_nbytes = LE32(sbuf);
	if (tap->tp_nbytes == 0xffffffff) {
		dprint(("%s: tap_readblock: end-of-medium marker at 0x%lx\n",
			tap->tp_path, ftell(tap->tp_fp) - 4));
		tap->tp_status |= TP_EOM;
		return -1;
	}

	/* Empty tape block indicates tapemark. No data or trailer to read */
	if (tap->tp_nbytes == 0)
		return 0;

	/* read data */
	tap->tp_buf = realloc(tap->tp_buf, tap->tp_nbytes);
	if (!tap->tp_buf) {
		fprintf(stderr, "%s: block size %u too large, offset 0x%lx\n",
			tap->tp_path, tap->tp_nbytes, ftell(tap->tp_fp) - 4);
		tap->tp_status |= TP_ERR;
		return -2;
	}
	rv = fread(tap->tp_buf, 1, tap->tp_nbytes, tap->tp_fp);
	if (rv != tap->tp_nbytes) {
		fprintf(stderr, "%s: EOF reading %u bytes, offset 0x%lx\n",
			tap->tp_path, tap->tp_nbytes, ftell(tap->tp_fp) - rv);
		tap->tp_status |= TP_ERR;
		return -2;
	}
	*bufp = tap->tp_buf;

	/* read trailer */
	rv = fread(sbuf, 1, 4, tap->tp_fp);
	if (rv < 4) {
		fprintf(stderr, "%s: EOF reading trailer at offset 0x%lx\n",
			tap->tp_path, ftell(tap->tp_fp) - 4);
		tap->tp_status |= TP_EOM;
		return tap->tp_nbytes;
	}
	if (tap->tp_nbytes & 1) {
		/* Some tape images omit the required even-byte padding. */
		if (tap->tp_nbytes == LE32(sbuf)) {
			dprint(("%s: tap_readblock: no padding at 0x%lx\n",
				tap->tp_path, ftell(tap->tp_fp) - 4));
			return tap->tp_nbytes;
		}

		/* Conforming image: skip over padding byte. */
		memmove(sbuf, sbuf+1, 3);
		rv = fread(sbuf+3, 1, 1, tap->tp_fp);
		if (rv < 1) {
			fprintf(stderr,
				"%s: EOF reading trailer, offset 0x%lx\n",
				tap->tp_path, ftell(tap->tp_fp) - 1);
			tap->tp_status |= TP_EOM;
			return tap->tp_nbytes;
		}
	}
	if (tap->tp_nbytes != LE32(sbuf)) {
		fprintf(stderr, "%s: trailer size %u (offset 0x%lx) "
				"doesn't match header size %u\n",
			tap->tp_path, LE32(sbuf), ftell(tap->tp_fp) - 4,
			tap->tp_nbytes);
		tap->tp_status |= TP_ERR;
	}

	return tap->tp_nbytes;
}


/* Write SIMH-format tape block */
/* returns bytes written inc. header/trailer, -1 if error */
ssize_t tap_writeblock(TAPE *tap, char *buf, ssize_t nbytes)
{
	char len[4], pad = '\0';
	ssize_t rv;

	if (!(tap->tp_status & TP_WRITE)) {
		fprintf(stderr,
			"%s: tap_writeblock not allowed while reading tape",
			tap->tp_path);
		return (ssize_t) -1;
	}

	len[0] = nbytes & 0xff;
	len[1] = (nbytes >> 8) & 0xff;
	len[2] = (nbytes >> 16) & 0xff;
	len[3] = (nbytes >> 24) & 0xff;

	/* write header size */
	rv = fwrite(len, 1, 4, tap->tp_fp);

	/* tapemark, don't write trail size */
	if (nbytes == 0)
		return rv;

	/* write data and pad */
	rv += fwrite(buf, 1, nbytes, tap->tp_fp);
	if (nbytes & 1) {
		dprint(("tap_writeblock: add pad, nbytes %ld\n", nbytes));
		rv += fwrite(&pad, 1, 1, tap->tp_fp);
	}

	/* write trail size */
	rv += fwrite(len, 1, 4, tap->tp_fp);

	return rv;
}
