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
 * Routines for converting tapes between 2000F and 2000 Access.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "simtap.h"
#include "sink.h"
#include "tfilefmt.h"
#include "tsbprog.h"
#include "tsbtap.h"
#include "convert.h"


static char zero[4];


void raw_copy(tfile_ctx_t *tf, tfile_ctx_t *otf)
{
	unsigned char buf[512];
	int nread;

	while ((nread = tfile_getbytes(tf, buf, sizeof buf)) > 0)
		tfile_putbytes(otf, buf, nread);
}


#define VERR(str)							\
    do {								\
	if (verbose > 1 || verbose && !ec)				\
		printf("%s line %d: %s\n", pname, lineno, str);		\
        ec++;								\
    } while (0)


/*
 * -a: Convert 2000F tape to 2000 Access.
 */


char *convert_prog_ftoa(char *pname, unsigned char *dbuf,
			tfile_ctx_t *tf, tfile_ctx_t *otf)
{
	prog_ctx_t prog, saveprog;	/* program being read */
	stmt_ctx_t ctx;			/* statement being read */
	char *err = NULL;
	int sz, lineno, ec = 0;
	unsigned char *pbuf;		/* converted program */
	int pbufsz = 8 * TBLOCKSIZE;	/* should hold largest TSB program */
	int poff = 0;

	if (prog_init(&prog, tf) < 0)
		return "";

	if (dbuf[6] & 0x80) {		/* CSAVEd */
		err = un_csave(&prog, dbuf);
		if (err)
			return err;
		dbuf[6] &= 0x7f;
	} else
		prog_setsz(&prog, 2 * -(int16_t) BE16(dbuf+22));

	/* allocate initial program buffer */
	if (!(pbuf = malloc(pbufsz)))
		return "Out of memory for converting tape";

	for (saveprog = prog;
	     (lineno = stmt_init(&ctx, &prog)) >= 0;
	     saveprog = prog) {
		SINK *snp;
		unsigned char *pb, *tbuf;
		int stlen;
		int stmt = -1;
		int len_state = 0;	/* 1=LEN 2=( 3=v$ */

		dprint(("convert_prog_ftoa: line %d\n", lineno));

		/* grow program buffer if no room for stmt */
		if (poff + STLEN_ACCESS + 1 > pbufsz) {
			pbufsz += TBLOCKSIZE;
			if (!(pbuf = realloc(pbuf, pbufsz))) {
				prog_fini(&prog);
				return "Out of memory for converting tape";
			}
		}

		/* start new statement; reserve space for lineno, length */
		pb = pbuf + poff;
		snp = sink_initstr(pb, STLEN_ACCESS+1);
		sink_write(snp, zero, 4);

		while (stmt_getbytes(&ctx, &tbuf, 2) == 2) {
			int nread;
			unsigned token = BE16(tbuf);
			unsigned op   = (token >> 9) & 0x3f;
			unsigned name = (token >> 4) & 0x1f;
			unsigned type = token        & 0xf;

			dprint(("convert_prog_ftoa: 0x%04x <%d,0%02o,0%o,0%o>\n",
				token, token >> 15, op, name, type));

			/* validate operator */
			if (stmt < 0) {
				stmt = op;

				switch (op) {
				    case 044:   /* IMAGE */
				    case 051:   /* REM */
				    case 070:   /* FILES */
					/* copy entire statement as-is */
					sink_write(snp, tbuf, 2);
					while (nread = stmt_getbytes(&ctx,
								     &tbuf,
								     256))
						sink_write(snp, tbuf, nread);
					continue;
				}
			} else {
				switch (op) {
				    case 010:	/* ) */
					/* insert end-of-formula for Access */
					if (len_state == 3) {
						sink_write(snp, zero, 2);
						len_state = 0;
					}
					break;

				    case 013:	/* ( */
					if (len_state == 1)
						len_state = 2;
					break;
				}
			}

			/* validate operand */
			if ((token & 0x8000) && type == 017) {
				switch (name) {
				    case 015:	/* LEN */
					len_state = 1;
					break;

				    case 024:	/* ZER */
				    case 025:	/* CON */
				    case 026:	/* IDN */
				    case 027:	/* INV */
				    case 030:	/* TRN */
					/* update in token */
					name += 7;
					tbuf[0] &= ~1;
					tbuf[0] |= (name >> 4) & 1;
					tbuf[1] &= ~(0xf << 4);
					tbuf[1] |=  name << 4;
					break;
				}
			}

			/* operator and operand code OK, copy them */
			sink_write(snp, tbuf, 2);

			/* copy operand */
			if (token & 0x8000) {

				/* FP number, copy 4 bytes */
				if (type == 0) {
					if (stmt_getbytes(&ctx, &tbuf, 4) != 4) {
						err = "number extends past "
						      "end of statement";
						break;
					}
					sink_write(snp, tbuf, 4);

				/* line # or DIM */
				} else if (type == 3) {
					/* copy first value */
					if (stmt_getbytes(&ctx, &tbuf, 2) != 2) {
						err = "value extends past "
						      "end of statement";
						break;
					}
					sink_write(snp, tbuf, 2);

					if (stmt == 045 ||	/* COM */
					    stmt == 045 ||	/* DIM */
					    op == 043)		/* USING */
						continue;

					/* GOTO/GOSUB OF, copy all values */
					while (stmt_getbytes(&ctx, &tbuf, 2)
									  == 2)
						sink_write(snp, tbuf, 2);
				}

			/* string constant */
			} else if (op == 1) {
				int i, len = ((token & 0xff) + 1) & ~1;
				unsigned char c;

				if (stmt_getbytes(&ctx, &tbuf, len) != len) {
					err =  "string extends past "
					       "end of statement";
					break;
				}
				for (i = 0; i < len; i++) {
					switch (c = tbuf[i]) {
					    case '\016':	/* ^N */
						c = '\n';
						break;

					    case '\017':	/* ^O */
						c = '\r';
						break;
					}
					sink_putc(c, snp);
				}

			/* null or string variable */
			} else if (type == 0) {

				/* string variable */
				if (name && len_state == 2)
					len_state = 3;
			}
		}

		stlen = sink_fini(snp);
		stmt_fini(&ctx);

		if (err)
			goto finish;

		/* too long */
		if (stlen > STLEN_ACCESS) {
			/* report error unless -e */
			if (!ignore_errs) {
				err = "statement too long";
				goto finish;
			}
			VERR("statement too long");

			/* redo as REM */
			prog = saveprog;
			(void) stmt_init(&ctx, &prog);
			snp = sink_initstr(pb, STLEN_ACCESS);
			sink_write(snp, zero, 4);

			sink_putc(051 << 1, snp);   	/* REM */
			sink_putc('!', snp);
			sink_putc('T', snp);		/* reason */
			(void) print_stmt(snp, &ctx);

			stlen = sink_fini(snp);
			stmt_fini(&ctx);

			/* append null if not at 16-bit word boundary */
			if (stlen & 1) {
				pb[stlen] = '\0';
				stlen++;
			}
		}


		/* store lineno, length of converted stmt */
		assert(!(stlen & 1));		/* mult. of 16-bit words? */
		pb[0] = lineno >> 8;
		pb[1] = lineno & 0xff;
		pb[2] = (stlen / 2) >> 8;
		pb[3] = (stlen / 2) & 0xff;
		poff += stlen;
	}

	/* write directory entry */
	assert(!(poff & 1));		/* multiple of 16-bit words? */
	sz = -(poff / 2);
	dbuf[22] = sz >> 8;
	dbuf[23] = sz & 0xff;
	tfile_putbytes(otf, dbuf, 24);

	/* write program */
	tfile_putbytes(otf, pbuf, poff);

finish:
	prog_fini(&prog);
	free(pbuf);

	return err;
}


int do_aopt(TAPE *tap, TAPE *ot)
{
	unsigned char *tbuf;
	ssize_t nread;
	tfile_ctx_t tf, otf;
	int octx_init = 0;
	int rv = 0;

	while ((nread = tap_readblock(tap, (char **) &tbuf)) >= 0) {
		unsigned char c, dbuf[24], oname[12], name[7];
		int i, renamed = 0;
		unsigned uid;

		/* tapemark? copy it */
		if (nread == 0) {
			tap_writeblock(ot, NULL, 0);
			continue;
		}

		/* TSB label? update and write */
		if (is_tsb_label(tbuf, nread)) {
			if (is_access > 0) {
				fprintf(stderr,
					"%s: already in Access format\n",
					tap->tp_path);
				rv = 1;
				goto done;
			}

			/* Access length includes header bytes */
			tbuf[0] = (-20/2) >> 8;
			tbuf[1] = (-20/2) & 0xff;

			/* change OS version */
			tbuf[16] = SYSLVL_ACCESS >> 8;
			tbuf[17] = SYSLVL_ACCESS & 0xff;
			tbuf[18] = FEATLVL_ACCESS >> 8;
			tbuf[19] = FEATLVL_ACCESS & 0xff;
			tap_writeblock(ot, tbuf, 20);
			tap_writeblock(ot, NULL, 0);	/* tapemark */

			/* skip Hibernate or Sleep data structures */
			tfile_ctx_init(&tf, tap, tbuf, nread, 0);
			goto next;
		}

		if (!octx_init) {
			tfile_ctx_init(&otf, ot, NULL, TBLOCKSIZE+24, 0);
			octx_init = 1;
		}

		/* read directory entry */
		tfile_ctx_init(&tf, tap, tbuf, nread, 2);
		if (tfile_getbytes(&tf, dbuf, 24) < 24)
			goto next;

		/* extract name, replace invalid characters with 'Z' */
		uid = BE16(dbuf);
		sprintf(oname, "%c%03d/", '@' + (uid >> 10), uid & 0x3ff);
		for (i = 0; i < 6; i++) {
			c = dbuf[i+2] & 0x7f;
			if (c == ' ')
				break;
			oname[i+5] = c;
			if (!(c >= 'A' && c <= 'Z' || c >= '0' && c <= '9')) {
				c = 'Z';
				renamed = 1;
				dbuf[i+2] = dbuf[i+2] & 0x80 | c;
			}
			name[i] = c;
		}
		oname[i+5] = name[i] = '\0';

		/* handle flags */
		dbuf[14] = dbuf[15] = 0;	/* clear flags */
		if (dbuf[2] & 0x80) {		/* translate 'P' flag */
			dbuf[2] &= 0x7f;
			dbuf[15] |= 0x2;
		}

		/* BASIC-formatted file? copy unaltered */
		if (dbuf[4] & 0x80) {
			tfile_putbytes(&otf, dbuf, 24); /* new dir. entry */
			raw_copy(&tf, &otf);

		/* BASIC program */
		} else {
			char *err = convert_prog_ftoa(oname, dbuf, &tf, &otf);
			if (err) {
				printf("Skipping %s: %s\n", oname, err);
				goto next;
			}
		}

		tfile_writef(&otf, 24);

		if (verbose) {
			printf("Converted %s", oname);
			if (renamed)
				printf(" -> %s", name);
			printf("\n");
		}
next:
		tfile_skipf(&tf);
		tfile_ctx_fini(&tf);
	}

done:
	if (octx_init)
		tfile_ctx_fini(&otf);

	return rv;

}


/*
 * -c: Convert 2000 Access tape to 2000F.
 */


char *convert_prog_atof(char *pname, unsigned char *dbuf,
			tfile_ctx_t *tf, tfile_ctx_t *otf)
{
	prog_ctx_t prog, saveprog;	/* program being read */
	stmt_ctx_t ctx;			/* statement being read */
	char *err = NULL;
	int sz, lineno, ec = 0;
	unsigned char *pbuf;		/* converted program */
	int pbufsz = 8 * TBLOCKSIZE;	/* should hold largest TSB program */
	int poff = 0;

	if (prog_init(&prog, tf) < 0)
		return "";

	if (dbuf[6] & 0x80) {		/* CSAVEd */
		err = un_csave(&prog, dbuf);
		if (err)
			return err;
		dbuf[6] &= 0x7f;
	} else
		prog_setsz(&prog, 2 * -(int16_t) BE16(dbuf+22));

	/* allocate initial program buffer */
	if (!(pbuf = malloc(pbufsz)))
		return "Out of memory for converting tape";

	for (saveprog = prog;
	     (lineno = stmt_init(&ctx, &prog)) >= 0;
	     saveprog = prog) {
		SINK *snp;
		unsigned char *pb, *tbuf;
		int stlen;
		int stmt = -1;
		int unsupp = 0;
		int dim_state = 0;	/* 1=DIM/COM 2=v$ 3=[ */
		int len_state = 0;	/* 1=LEN 2=( 3=v$ */
		int prt_state = 0;	/* 1=PRINT 2=#(file) */

		dprint(("convert_prog_atof: line %d\n", lineno));

		/* grow program buffer if no room for stmt */
		if (poff + STLEN_2000F + 1 > pbufsz) {
			pbufsz += TBLOCKSIZE;
			if (!(pbuf = realloc(pbuf, pbufsz))) {
				prog_fini(&prog);
				return "Out of memory for converting tape";
			}
		}

		/* start new statement; reserve space for lineno, length */
		pb = pbuf + poff;
		snp = sink_initstr(pb, STLEN_2000F+1);
		sink_write(snp, zero, 4);

		while (stmt_getbytes(&ctx, &tbuf, 2) == 2) {
			int nread;
			unsigned token = BE16(tbuf);
			unsigned op   = (token >> 9) & 0x3f;
			unsigned name = (token >> 4) & 0x1f;
			unsigned type = token        & 0xf;

			dprint(("convert_prog_atof: 0x%04x <%d,0%02o,0%o,0%o>\n",
				token, token >> 15, op, name, type));

			/* validate operator */
			if (stmt < 0) {
				stmt = op;

				switch (op) {
				    case 044:   /* IMAGE */
				    case 051:   /* REM */
				    case 070:   /* FILES */
					/* copy entire statement as-is */
					sink_write(snp, tbuf, 2);
					while (nread = stmt_getbytes(&ctx,
								     &tbuf,
								     256))
						sink_write(snp, tbuf, nread);
					continue;

				    case 045:   /* COM */
				    case 047:   /* DIM */
					dim_state = 1;
					break;

				    case 042:   /* ASSIGN */
					break;

				    case 065:   /* PRINT */
					prt_state = 1;
					break;

				    default:
					if (stmt > 044)
						break;

					/* all others not supported on 2000F */
					VERR("unsupported statement type");
					unsupp = 's';
					break;
				}
			} else {
				switch (op) {
				    case 000:	/* end of formula */
					/* omit after LEN(v$ on 2000F */
					if (len_state == 3) {
						len_state = 0;
						continue;
					}

				    case 001:	/* " */
					/* LEN("string") not avail. on 2000F */
					if (len_state == 2) {
						VERR("LEN of string constant");
						unsupp = 'i';
					}
					break;

				    case 004:	/* #(file) */
					if (prt_state == 1)
						prt_state = 2;
					break;

				    case 011:	/* ] */
					if (dim_state > 0)
						dim_state = 1;
					break;

				    case 012:	/* [ */
					if (dim_state == 2)
						dim_state = 3;
					break;

				    case 013:	/* ( */
					if (len_state == 1)
						len_state = 2;
					break;

				    case 042:	/* ** */
					/* replace with ^ */
					op = 024;
					tbuf[0] &= ~(0x3f << 1);
					tbuf[0] |=     op << 1;
					break;

				    case 043:	/* USING */
					if (prt_state == 2) {
						VERR("PRINT USING to file");
						unsupp = 'u';
					}
					break;


				    case 044:   /* RR */
				    case 045:   /* WR */
				    case 046:   /* NR */
				    case 047:   /* ERROR */
					/* not supported on 2000F */
					VERR("unsupported operator");
					unsupp = 'o';
					break;
				}

				if (unsupp)
					break;
			}

			/* validate operand */
			if (!(token & 0x8000)) {
				/* string constant */
				if (op == 1) {
					/* must be <= 72 chars on 2000F */
					if ((token & 0xff) > 72) {
						VERR("string too long");
						unsupp = 'l';
						break;
					}

				/* null or string variable */
				} else if (type == 0) {

					/* A0$-Z1$ not supported on 2000F */
					if (name > 032) {
						VERR("unsupported string "
						     "variable");
						unsupp = 'v';
						break;
					}

					/* string variable */
					if (name) {
						if (len_state == 2)
							len_state = 3;
						if (dim_state == 1)
							dim_state = 2;
					}
				}

			} else if (type == 017) {
				switch (name) {
				    case 015:	/* LEN */
					len_state = 1;
					break;

				    case 000:	/* CTL */
				    case 023:	/* ITM */
				    case 024:	/* REC */
				    case 025:	/* NUM */
				    case 026:	/* POS */
				    case 027:	/* CHR$ */
				    case 030:	/* UPS$ */
				    case 031:	/* SYS */
				    case 032:	/* unused */
					/* unsupported on 2000F */
					VERR("unsupported function");
					unsupp = 'f';
					break;

				    case 033:	/* ZER */
				    case 034:	/* CON */
				    case 035:	/* IDN */
				    case 036:	/* INV */
				    case 037:	/* TRN */
					/* update in token */
					name -= 7;
					tbuf[0] &= ~1;
					tbuf[0] |= (name >> 4) & 1;
					tbuf[1] &= ~(0xf << 4);
					tbuf[1] |=  name << 4;
					break;
				}
				if (unsupp)
					break;
			}

			/* operator and operand code OK, copy them */
			sink_write(snp, tbuf, 2);

			/* copy operand */
			if (token & 0x8000) {

				/* FP number, copy 4 bytes */
				if (type == 0) {
					if (stmt_getbytes(&ctx, &tbuf, 4) != 4) {
						err = "number extends past "
						      "end of statement";
						break;
					}
					sink_write(snp, tbuf, 4);

				/* line # or DIM */
				} else if (type == 3) {
					/* copy first value */
					if (stmt_getbytes(&ctx, &tbuf, 2) != 2) {
						err = "value extends past "
						      "end of statement";
						break;
					}
					sink_write(snp, tbuf, 2);

					if (op == 043)	/* USING */
						continue;
					if (dim_state) {
						/* v$ DIM <= 72 on 2000F */
						if (dim_state == 3 &&
						    BE16(tbuf) > 72) {
							VERR("string dimension "
							     "too large");
							unsupp = 'd';
							break;
						}
						continue;
					}


					/* GOTO/GOSUB OF, copy all values */
					while (stmt_getbytes(&ctx, &tbuf, 2)
									  == 2)
						sink_write(snp, tbuf, 2);
				}

			/* string constant */
			} else if (op == 1) {
				int i, len = ((token & 0xff) + 1) & ~1;
				unsigned char c;

				if (stmt_getbytes(&ctx, &tbuf, len) != len) {
					err =  "string extends past "
					       "end of statement";
					break;
				}
				for (i = 0; i < len; i++) {
					switch (c = tbuf[i]) {
					    case '\n':
						c = '\016';	/* ^N */
						break;

					    case '\r':
						c = '\017';	/* ^O */
						break;
					}
					sink_putc(c, snp);
				}
			}
		}

		stlen = sink_fini(snp);
		stmt_fini(&ctx);

		if (err)
			goto finish;

		/* too long or unsupported */
		if (stlen > STLEN_2000F || unsupp) {
			/* report error unless -e */
			if (!ignore_errs) {
				err = unsupp ? "unsupported construct"
					     : "statement too long";
				goto finish;
			}

			/* redo as REM */
			prog = saveprog;
			(void) stmt_init(&ctx, &prog);
			snp = sink_initstr(pb, STLEN_2000F);
			sink_write(snp, zero, 4);

			if (!unsupp) {
				VERR("statement too long");
				unsupp = 't';
			}
			sink_putc(051 << 1, snp);   	/* REM */
			sink_putc('!', snp);
			sink_putc(unsupp, snp);		/* reason */
			(void) print_stmt(snp, &ctx);

			stlen = sink_fini(snp);
			stmt_fini(&ctx);

			/* append null if not at 16-bit word boundary */
			if (stlen & 1) {
				pb[stlen] = '\0';
				stlen++;
			}
		}


		/* store lineno, length of converted stmt */
		assert(!(stlen & 1));		/* mult. of 16-bit words? */
		pb[0] = lineno >> 8;
		pb[1] = lineno & 0xff;
		pb[2] = (stlen / 2) >> 8;
		pb[3] = (stlen / 2) & 0xff;
		poff += stlen;
	}

	/* write directory entry */
	assert(!(poff & 1));		/* multiple of 16-bit words? */
	sz = -(poff / 2);
	dbuf[22] = sz >> 8;
	dbuf[23] = sz & 0xff;
	tfile_putbytes(otf, dbuf, 24);

	/* write program */
	tfile_putbytes(otf, pbuf, poff);

finish:
	prog_fini(&prog);
	free(pbuf);

	return err;
}


int do_copt(TAPE *tap, TAPE *ot)
{
	unsigned char *tbuf;
	ssize_t nread;
	tfile_ctx_t tf, otf;
	int octx_init = 0;
	int rv = 0;

	while ((nread = tap_readblock(tap, (char **) &tbuf)) >= 0) {
		unsigned char dbuf[24], name[12];
		int i;
		unsigned uid;

		/* tapemark? copy it */
		if (nread == 0) {
			tap_writeblock(ot, NULL, 0);
			continue;
		}

		/* TSB label? update and write */
		if (is_tsb_label(tbuf, nread)) {
			if (is_access <= 0) {
				fprintf(stderr,
					"%s: already in 2000F format\n",
					tap->tp_path);
				rv = 1;
				goto done;
			}

			/* 2000F length omits header bytes */
			tbuf[0] = (-18/2) >> 8;
			tbuf[1] = (-18/2) & 0xff;

			/* change OS version */
			tbuf[16] = SYSLVL_2000F >> 8;
			tbuf[17] = SYSLVL_2000F & 0xff;
			tbuf[18] = FEATLVL_2000F >> 8;
			tbuf[19] = FEATLVL_2000F & 0xff;
			tap_writeblock(ot, tbuf, 20);
			tap_writeblock(ot, NULL, 0);	/* tapemark */

			/* skip Hibernate or Sleep data structures */
			tfile_ctx_init(&tf, tap, tbuf, nread, 0);
			goto next;
		}

		if (!octx_init) {
			tfile_ctx_init(&otf, ot, NULL, TBLOCKSIZE+24, 2);
			octx_init = 1;
		}

		/* read directory entry */
		tfile_ctx_init(&tf, tap, tbuf, nread, 0);
		if (tfile_getbytes(&tf, dbuf, 24) < 24)
			goto next;

		/* extract name */
		uid = BE16(dbuf);
		sprintf(name, "%c%03d/", '@' + (uid >> 10), uid & 0x3ff);
		for (i = 0; i < 6; i++) {
			name[i+5] = dbuf[i+2] & 0x7f;
			if (name[i+5] == ' ')
				break;
		}
		name[i+5] = '\0';

		/* skip ASCII files */
		if (dbuf[2] & 0x80) {
			printf("Skipped ASCII file %s\n", name);
			goto next;
		}

		/* handle flags */
		if (dbuf[15] & 0x6) {		/* protected or locked */
			dbuf[2] |= 0x80;	/* set 'P' flag */
		}
		dbuf[14] = dbuf[15] = 0;	/* clear drum address */

		/* BASIC-formatted file? copy unaltered */
		if (dbuf[4] & 0x80) {
			tfile_putbytes(&otf, dbuf, 24); /* new dir. entry */
			raw_copy(&tf, &otf);

		/* BASIC program */
		} else {
			char *err = convert_prog_atof(name, dbuf, &tf, &otf);
			if (err) {
				printf("Skipping %s: %s\n", name, err);
				goto next;
			}
		}

		tfile_writef(&otf, 24);

		if (verbose) {
			printf("Converted %s\n", name);
		}

next:
		tfile_skipf(&tf);
		tfile_ctx_fini(&tf);
	}

done:
	if (octx_init)
		tfile_ctx_fini(&otf);

	return rv;
}
