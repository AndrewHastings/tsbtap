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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "sink.h"
#include "tsbtap.h"


SINK *sink_initf(FILE *fp)
{
	SINK *rv;

	rv = malloc(sizeof(SINK));
	if (rv) {
		memset(rv, 0, sizeof(SINK));
		rv->snk_fp = fp;
	}
	return rv;
}


SINK *sink_initstr(char *buf, int nbytes)
{
	SINK *rv;

	rv = malloc(sizeof(SINK));
	if (rv) {
		memset(rv, 0, sizeof(SINK));
		rv->snk_buf = rv->snk_bp = buf;
		rv->snk_nleft = nbytes;
	}
	return rv;
}


int sink_printf(SINK *snp, char *fmt, ...)
{
	va_list ap;
	int rv;

	/* file */
	if (snp->snk_fp) {
		va_start(ap, fmt);
		rv = vfprintf(snp->snk_fp, fmt, ap);
		va_end(ap);
		if (rv > 0)
			snp->snk_nwrite += rv;
		return rv;
	}

	/* string */
	if (snp->snk_nleft) {
		va_start(ap, fmt);
		rv = vsnprintf(snp->snk_bp, snp->snk_nleft, fmt, ap);
		va_end(ap);
		rv = MIN(rv, snp->snk_nleft);
		snp->snk_bp += rv;
		snp->snk_nleft -= rv;
		return rv;
	}

	return 0;
}


int sink_write(SINK *snp, char *buf, int nbytes)
{
	int rv;

	/* file */
	if (snp->snk_fp) {
		rv = fwrite(buf, 1, nbytes, snp->snk_fp);
		if (rv > 0)
			snp->snk_nwrite += rv;
		return rv;
	}

	/* string */
	if (snp->snk_nleft) {
		rv = MIN(nbytes, snp->snk_nleft);
		memcpy(snp->snk_bp, buf, rv);
		snp->snk_bp += rv;
		snp->snk_nleft -= rv;
		return rv;
	}

	return 0;
}


int sink_putc(int c, SINK *snp)
{
	int rv;

	/* file */
	if (snp->snk_fp) {
		rv = putc(c, snp->snk_fp);
		if (rv > 0)
			snp->snk_nwrite++;
		return rv;
	}

	/* string */
	if (snp->snk_nleft) {
		*(snp->snk_bp) = c;
		snp->snk_bp++;
		snp->snk_nleft--;
		return 1;
	}

	return 0;
}


int sink_fini(SINK *snp)
{
	int rv;

	/* file */
	if (snp->snk_fp) {
		rv = snp->snk_nwrite;

	/* string */
	} else if (snp->snk_buf) {
		rv = snp->snk_bp - snp->snk_buf;

	/* not initialized? */
	} else
		rv = -1;

	memset(snp, 0, sizeof(SINK));
	free(snp);
	return rv;
}
