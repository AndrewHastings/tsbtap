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
 * TSB tokenized BASIC program routines.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include "simtap.h"
#include "sink.h"
#include "outfile.h"
#include "tfilefmt.h"
#include "tsbprog.h"
#include "tsbtap.h"


/*
 * In-between the calls to prog_init(), prog_fini():
 * - prog_ctx_t can be saved and restored to move to an earlier position
 *   in the program text.
 * - The buffer provided by prog_getbytes can be modified in-place, to
 *   affect subsequent re-reads of the same part of the program text.
 * This allows CSAVEd programs to be un-CSAVEd in-place. It also allows
 * a conversion between 2000 TSB versions to re-interpret a statement if
 * an unsupported token is encountered mid-statement.
 */

int prog_init(prog_ctx_t *prog, tfile_ctx_t *tfile)
{
	unsigned char *buf;
	int rv, nread;
	int bufsz = 8 * TBLOCKSIZE;	/* should hold largest TSB program */
	int readsz = bufsz;

	memset(prog, 0, sizeof(prog_ctx_t));

	/* allocate initial buffer */
	if (!(buf = malloc(bufsz))) {
		printf("out of memory for BASIC program\n");
		return -2;
	}

	/* read in entire program */
	for (nread = 0;
	     (rv = tfile_getbytes(tfile, buf+nread, readsz)) == readsz;
	     nread += rv) {
		/* grow buffer, continue reading */
		bufsz += TBLOCKSIZE;
		if (!(buf = realloc(buf, bufsz))) {
			printf("out of memory for BASIC program\n");
			return -2;
		}
		readsz = TBLOCKSIZE;
	}
	if (rv == -2)
		return rv;
	if (rv >= 0)
		nread += rv;

	prog->pg_buf = prog->pg_bp = buf;
	prog->pg_sz = prog->pg_nread = nread;
	dprint(("prog_init: bufsz=%d progsz=%d\n", bufsz, nread));

	return 0;
}


void prog_setsz(prog_ctx_t *prog, int nbytes)
{
	dprint(("prog_setsz: read %d, dir len %d\n", prog->pg_sz, nbytes));

	if (nbytes > 0 && nbytes <= prog->pg_sz)
		prog->pg_sz = nbytes;
	else
		printf("invalid size in directory entry\n");
}


int prog_nleft(prog_ctx_t *prog)
{
	return prog->pg_sz - (prog->pg_bp - prog->pg_buf);
}


/* access bytes sequentially up to pg_sz. not affected by prog_getbytesat */
int prog_getbytes(prog_ctx_t *prog, unsigned char **bufp, int nbytes)
{
	int readsz, nleft;

	nleft = prog_nleft(prog);
	readsz = MIN(nbytes, nleft);
	*bufp = prog->pg_bp;
	prog->pg_bp += readsz;
	return readsz;
}


/* access bytes randomly up to pg_nread */
int prog_getbytesat(prog_ctx_t *prog, unsigned char **bufp, int nbytes, int off)
{
	int nleft;

	if (off < 0 || off > prog->pg_nread)
		return -2;
	*bufp = prog->pg_buf + off;
	nleft = prog->pg_nread - off;
	return MIN(nbytes, nleft);
}


void prog_fini(prog_ctx_t *prog)
{
	if (prog->pg_buf)
		free(prog->pg_buf);
	memset(prog, 0, sizeof(prog_ctx_t));
}


/* returns TSB line number, -1 if error */
int stmt_init(stmt_ctx_t *ctx, prog_ctx_t *prog)
{
	unsigned char *buf;
	int nbytes;

	/* get line number, count of 16-bit words */
	nbytes = prog_getbytes(prog, &buf, 4);
	if (nbytes < 4) {
		dprint(("stmt_init: EOF\n"));
		return -1;
	}
	ctx->st_ctx = prog;
	ctx->st_nleft = 2 * BE16(buf+2) - 4;
	return BE16(buf);
}


/* returns number of bytes accessible. nbytes must be even */
int stmt_getbytes(stmt_ctx_t *ctx, unsigned char **buf, int nbytes)
{
	int nread;

	assert((nbytes & 1) == 0);
	nbytes = MIN(ctx->st_nleft, nbytes);
	nread = prog_getbytes(ctx->st_ctx, buf, nbytes);
	ctx->st_nleft -= nread;
	if (nread != nbytes)
		dprint(("stmt_getbytes: EOF\n"));
	return nread;
}


void stmt_fini(stmt_ctx_t *ctx)
{
	memset(ctx, 0, sizeof(stmt_ctx_t));
}


static char *access_stmts[] = {
	"?00", "?01", "?02", "?03", "?04", "?05", "?06", "?07",
	"?10", "?11", "?12", "?13", "?14", "?15", "?16", "?17",
	"?20", "?21", "?22", "?23", "?24", "?25", "?26", "?27",
	"?30", "?31", "SYSTEM", "CONVERT", "LOCK", "UNLOCK", "CREATE", "PURGE",
	"ADVANCE", "UPDATE", "ASSIGN", "LINPUT", "IMAGE", "COM", "LET", "DIM",
	"DEF", "REM", "GOTO", "IF", "FOR", "NEXT", "GOSUB", "RETURN",
	"END", "STOP", "DATA", "INPUT", "READ", "PRINT", "RESTORE", "MAT",
	"FILES", "CHAIN", "ENTER", " " /* (LET) */, "?74", "?75", "?76", "?77"
};
static char *access_ops[] = {
	"", "" /* " */, ",", ";", "#", "?05", "?06", "?07",
	")", "]", "[", "(", "+", "-", ",", "=",
	"+", "-", "*", "/", "^", ">", "<", "#",
	"=", "?31", "AND", "OR", "MIN", "MAX", "<>", ">=",
	"<=", "NOT", "**", "USING", "RR", "WR", "NR", "ERROR",
	"?50", "?51", "?52", "?53", "?54", "?55", "?56", "?57",
	"END", "?61", "?62", "INPUT", "READ", "PRINT", "?66", "?67",
	"?70", "?71", "?72", "?73", "OF", "THEN", "TO", "STEP"
};
static char *tsb2000f_ops[] = {
	"", "" /* " */, ",", ";", "#", "?05", "?06", "?07",
	")", "]", "[", "(", "+", "-", ",", "=",
	"+", "-", "*", "/", "^", ">", "<", "#",
	"=", "?31", "AND", "OR", "MIN", "MAX", "<>", ">=",
	"<=", "NOT", "ASSIGN", "USING", "IMAGE", "COM", "LET", "DIM",
	"DEF", "REM", "GOTO", "IF", "FOR", "NEXT", "GOSUB", "RETURN",
	"END", "STOP", "DATA", "INPUT", "READ", "PRINT", "RESTORE", "MAT",
	"FILES", "CHAIN", "ENTER", " " /* (LET) */, "OF", "THEN", "TO", "STEP"
};
static char access_fns[] = "CTLTABLINSPATANATNEXPLOGABSSQRINTRNDSGNLENTYPTIM"
			   "SINCOSBRKITMRECNUMPOSCHRUPSSYS?32ZERCONIDNINVTRN";
static char tsb2000f_fns[] = "?00TABLINSPATANATNEXPLOGABSSQRINTRNDSGNLENTYPTIM"
			     "SINCOSBRK?23ZERCONIDNINVTRN?31?32?33?34?35?36?37";


char *print_str_operand(SINK *snp, unsigned token, stmt_ctx_t *ctx)
{
	int len = token & 0xff;
	int i, nread;
	unsigned char c, *tbuf;

	if (len == 0) {
		sink_printf(snp, "\"\"");
		return NULL;
	}

	/* consume even number of bytes */
	nread = (len+1) & ~1;
	if (stmt_getbytes(ctx, &tbuf, nread) != nread)
		return "string extends past end of statement";

	/* Access: use 'decimal notation for non-printable chars, quotes */
	if (is_access > 0) {
		int inquote = 0;

		for (i = 0; i < len; i++) {
			c = tbuf[i];
			if (c >= 32 && c < 127 && c != '"') {
				if (!inquote)
					sink_putc('"', snp);
				inquote = 1;
				sink_putc(c, snp);
			} else {
				if (inquote)
					sink_putc('"', snp);
				inquote = 0;
				sink_printf(snp, "'%d", c);
			}
		}
		if (inquote)
			sink_putc('"', snp);

	/* pre-Access: just print it */
	} else {
		sink_printf(snp, "\"%.*s\"", len, tbuf);
	}

	return NULL;
}


char *print_var_operand(SINK *snp, unsigned token)
{
	unsigned name = (token >> 4) & 0x1f;
	unsigned type =  token       & 0xf;

	/* string variable with digit 0 or 1 */
	if (name > 032) {
		sink_printf(snp, "%c%d$", 'A' + ((token - 0xb0) & 0x1f),
					  name > 034);
		return NULL;
	}

	switch (type) {
	    case 0:			/* string variable */
		if (!name)		/* null operand */
			break;
		sink_printf(snp, "%c$", '@' + name);
		break;

	    case 1: case 2: case 3:	/* array variable */
	    case 4:			/* simple variable, no digit */
		sink_printf(snp, "%c", '@' + name);
		break;

	    case 017:			/* user-defined function */
		sink_printf(snp, "FN%c", '@' + name);
		break;

	    default:			/* simple variable with digit 0-9 */
		sink_printf(snp, "%c%d", '@' + name, type - 5);
		break;
	}

	return NULL;
}


char *print_int_operand(SINK *snp, unsigned token, unsigned stmt,
			stmt_ctx_t *ctx)
{
	unsigned char *tbuf;
	char *err = NULL;
	int is_dim = (stmt == 045 || stmt == 047);	/* COM, DIM */

	if (stmt_getbytes(ctx, &tbuf, 2) != 2)
		return "value extends past end of statement";
	sink_printf(snp, "%d", BE16(tbuf));

	if (is_dim || ((token >> 9) & 0x3f) == 043)	/* USING */
		return err;

	while (stmt_getbytes(ctx, &tbuf, 2) == 2)	/* GOTO/GOSUB OF */
		sink_printf(snp, ",%d", BE16(tbuf));

	return err;
}


char *print_other_operand(SINK *snp, unsigned token)
{
	char *fns = is_access > 0 ? access_fns : tsb2000f_fns;
	unsigned name = (token >> 4) & 0x1f;
	unsigned type =  token       & 0xf;

	switch (type) {
	    case 0: case 3:		/* handled elsewhere */
		return "internal error";
		break;

	    case 1: case 2:		/* not used */
		return "unknown operand type";
		break;

	    case 4:			/* formal param, no digit */
		sink_printf(snp, "%c", '@' + name);
		break;

	    case 017:			/* built-in function */
		sink_printf(snp, "%.3s", fns + 3 * name);
		if (fns == access_fns && (name == 027 || name == 030))
			sink_putc('$', snp);
		break;

	    default:			/* formal param with digit 0-9 */
		sink_printf(snp, "%c%d", '@' + name, type - 5);
		break;
	}

	return NULL;
}


char *un_csave(prog_ctx_t *prog, unsigned char *dbuf)
{
	prog_ctx_t save_prog;
	stmt_ctx_t ctx;
	int lineno;			/* for debugging */
	char *err = NULL;
	unsigned char *buf;
	int len = 2 * -(int16_t) BE16(dbuf+22);
	int symptr = is_access > 0 ? 12 : 14;
	int symtab = 0;			/* offset in bytes */
	int start = BE16(dbuf+8);	/* 16-bit words */

	if (prog_getbytesat(prog, &buf, 2, len - symptr) == 2)
		symtab = (BE16(buf) - start) * 2;
	else
		return "can't find symtab for CSAVEd program";
	if (symtab <= 0)
		return "invalid symtab addr for CSAVEd program";
	prog_setsz(prog, symtab);
	save_prog = *prog;

	while ((lineno = stmt_init(&ctx, prog)) >= 0) {
		unsigned char *tbuf;
		int stmt = -1;

		dprint(("un_csave: line %d\n", lineno));
		while (stmt_getbytes(&ctx, &tbuf, 2) == 2) {
			unsigned char *nbuf;
			unsigned token = BE16(tbuf);
			unsigned op = (token >> 9) & 0x3f;

			dprint(("un_csave: 0x%04x <%d,0%02o,0%o,0%o>\n",
				token, token >> 15, op,
				(token >> 4) & 0x1f, token & 0xf));

			/* save statement code; process special cases */
			if (stmt < 0) {
				stmt = op;
				switch (op) {
				    case 070:	/* FILES */
				    case 051:	/* REM */
				    case 044:	/* IMAGE */
					/* consume rest of stmt */
					while (stmt_getbytes(&ctx, &nbuf, 256))
						;
					continue;
				}
			}

			/* number, parameter, or built-in function */
			if (token & 0x8000) {
				unsigned type = token & 0xf;
				int val;

				/* FP number: consume and continue */
				if (type == 0) {
					if (stmt_getbytes(&ctx, &tbuf, 4) != 4) {
						err = "number extends past "
						      "end of statement";
						break;
					}
					continue;
				}

				/* not an int: done */
				if (type != 3)
					continue;

				/* integer: consume, then handle below */
				if (stmt_getbytes(&ctx, &tbuf, 2) != 2) {
					err = "value extends past "
					      "end of statement";
					break;
				}

				/* COM or DIM: done */
				if (stmt == 045 || stmt == 047)
					continue;

				/* else, replace with dest lineno */
				val = BE16(tbuf);
				if (prog_getbytesat(prog, &nbuf, 2,
						    (val - start) * 2) == 2) {
					tbuf[0] = nbuf[0];
					tbuf[1] = nbuf[1];
				} else {
					dprint(("un_csave: dest %d\n",
						val - start));
					err = "corrupted destination "
					      "line number";
				}

				/* USING: only one lineno, done */
				if (op == 043) {
					dprint(("un_csave: USING\n"));
					continue;
				}

				/* GOTO/GOSUB OF: replace all dest linenos */
				dprint(("un_csave: GOTO OF\n"));
				while (stmt_getbytes(&ctx, &tbuf, 2) == 2) {
					val = BE16(tbuf);
					if (prog_getbytesat(prog, &nbuf, 2,
						     (val - start) * 2) == 2) {
						tbuf[0] = nbuf[0];
						tbuf[1] = nbuf[1];
					} else {
						dprint(("un_csave: dest %d\n",
							val - start));
						err = "corrupted destination "
						      "line number";
					}
				}

			/* string: consume even number of bytes */
			} else if (op == 1) {
				int nread = ((token & 0xff) + 1) & ~1;
				if (stmt_getbytes(&ctx, &nbuf, nread) != nread) {
					err = "string extends past "
					      "end of statement";
					break;
				}

			/* variable or user function: replace w/symtab name */
			} else {
				int idx = token & 0x1ff;

				if (idx) {
					if (prog_getbytesat(prog, &nbuf, 2,
						  symtab + 4 * (idx-1)) == 2) {
						tbuf[0] = (tbuf[0] & ~1) |
							  (nbuf[0] & 1);
						tbuf[1] = nbuf[1];
					} else
						err = "corrupted symbol table";
				}
			}
		}

		stmt_fini(&ctx);
	}

	/* return to start of program */
	*prog = save_prog;
	return err;
}


char *extract_program(tfile_ctx_t *tfile, char *fn, char *oname,
		      unsigned char *dbuf)
{
	prog_ctx_t prog;
	stmt_ctx_t ctx;
	int lineno, prev_lineno = 0;
	char *err = NULL;
	SINK *snp;

	dprint(("extract_program: %s\n", fn));

	if (prog_init(&prog, tfile) < 0)
		return "";

	if (dbuf[6] & 0x80) {	/* CSAVEd */
		err = un_csave(&prog, dbuf);
		if (err) {
			prog_fini(&prog);
			return err;
		}
	} else
		prog_setsz(&prog, 2 * -(int16_t) BE16(dbuf+22));

	snp = out_open(fn, "bas", oname);
	if (!snp) {
		prog_fini(&prog);
		return "";
	}

	while ((lineno = stmt_init(&ctx, &prog)) >= 0) {
		unsigned char *tbuf;
		int stmt = -1;
		int nread;
		char **opnames = is_access > 0 ? access_stmts : tsb2000f_ops;

		dprint(("extract_program: line %d\n", lineno));
		if (lineno > 9999 || lineno <= prev_lineno) {
			if (!ignore_errs) {
				err = "lines out of order";
				stmt_fini(&ctx);
				break;
			}
			fprintf(sink_getf(snp),
				"*** Warning: lines out of order -- "
				"tape may be corrupted ***\n");
		}
		sink_printf(snp, "%d ", lineno);
		prev_lineno = lineno;

		while (stmt_getbytes(&ctx, &tbuf, 2) == 2) {
			unsigned token = BE16(tbuf);
			unsigned op = (token >> 9) & 0x3f;
			char *space, *name = opnames[op];

			dprint(("extract_program: 0x%04x <%d,0%02o,0%o,0%o>\n",
				token, token >> 15, op,
				(token >> 4) & 0x1f, token & 0xf));
			space = name[0] && name[1] ? " " : "";
			sink_printf(snp, "%s%s", space, name);

			/* save statement code; process special cases */
			if (stmt < 0) {
				stmt = op;
				switch (op) {
				    case 070:	/* FILES */
					sink_putc(' ', snp);
					/* fall thru */
				    case 051:	/* REM */
					if (token & 0xff)
						sink_putc(token & 0xff, snp);
					/* fall thru */
				    case 044:	/* IMAGE */
					while (nread = stmt_getbytes(&ctx,
								&tbuf, 256)) {
						if (!tbuf[nread-1])
							nread--;
						sink_write(snp, tbuf, nread);
					}
					goto next;
				}
			}

			sink_printf(snp, "%s", space);
			if (token & 0x8000) {
				unsigned type = token & 0xf;

				if (type == 0) {	/* FP number */
					if (stmt_getbytes(&ctx, &tbuf, 4) != 4) {
						err = "number extends past "
						      "end of statement";
						break;
					}
					print_number(snp, tbuf);

				} else if (type == 3)	/* line # or DIM */
					err = print_int_operand(snp, token,
								stmt, &ctx);

				 else
					err = print_other_operand(snp, token);

			} else if (op == 1) {
				err = print_str_operand(snp, token, &ctx);

			} else {
				(void) print_var_operand(snp, token);
			}
next:
			/* Access: subsequent operators aren't stmt codes */
			if (is_access > 0)
				opnames = access_ops;

			if (err)
				break;
		}

		sink_putc('\n', snp);
		stmt_fini(&ctx);
		if (err)
			break;
	}

	out_close(snp);
	prog_fini(&prog);

	return err;
}


char *dump_program(tfile_ctx_t *tfile, char *fn, unsigned char *dbuf)
{
	SINK *snp;
	prog_ctx_t prog;
	unsigned char *buf;
	unsigned off;
	int i, uid = BE16(dbuf);
	int nused = 0, nleft = 0;	/* words in statement */

	dprint(("dump_program: %s\n", fn));

	printf("\n%c%03d/", '@' + (uid >> 10), uid & 0x3ff);
	print_direntry(dbuf);
	printf(" len=0x%04x start=0x%04x disk=0x%04x%04x\n",
	       -(int16_t) BE16(dbuf+22), BE16(dbuf+8),
	       BE16(dbuf+16), BE16(dbuf+18));

	if (prog_init(&prog, tfile) < 0)
		return "";
	snp = sink_initf(stdout);

	/* replace some op names for clarity */
	for (i = 0; i < 0100; i++) {
		if (tsb2000f_ops[i][0] == '?')
			tsb2000f_ops[i] = "";
		if (access_ops[i][0] == '?')
			access_ops[i] = "";
		if (access_stmts[i][0] == '?')
			access_stmts[i] = "";
	}
	access_ops[0] = tsb2000f_ops[0] = "(end)";	/* end of formula */
	access_ops[1] = tsb2000f_ops[1] = "\"";
	access_ops[4] = tsb2000f_ops[4] = "#(file)";
	access_stmts[073] = tsb2000f_ops[073] = "(LET)";

	for (off = 0; prog_getbytes(&prog, &buf, 2) == 2; off++) {
		unsigned val = BE16(buf);
		unsigned op   = (val >> 9) & 0x3f;
		unsigned name = (val >> 4) & 0x1f;
		unsigned type =  val       & 0xf;
		char *pfx, *sfx;

		/* start or end of statement? */
		pfx = " ";
		switch (nleft) {
		    case 1:   pfx = "}"; break;
		    case 0:   pfx = "{"; nused = 0; break;
		    case -1:  nleft = val - 1; break;
		}
		nleft--;
		sink_printf(snp, "%s ", pfx);

		/* offset */
		if (off & 0x7)
			sink_printf(snp, "     ");
		else
			sink_printf(snp, "%5x", off);

		/* contents as hex and decimal */
		pfx = sfx = "";
		if (nused == 0) {	    /* underline line number */
			pfx = "\033[4m";
			sfx = "\033[0m";
		}
		sink_printf(snp, "  %04x (%s%5d%s)  ", val, pfx, val, sfx);

		/* contents as ASCII */
		for (i = 0; i < 2; i++) {
			unsigned char c = buf[i];

			if (c < 32 || c >= 127)
				c = '.';
			sink_printf(snp, "%c%s", c, sfx);
		}

		/* contents as token codes */
		sink_printf(snp, "  %d-%2o-%2o-%2o  ", val >> 15, op, name,
						       type);

		/* contents as operator name(s) */
		pfx = sfx = "";
		if (nused == 2) {	    /* underline statement name */
			pfx = "\033[4m";
			sfx = "\033[0m";
		}
		if (is_access > 0)
			sink_printf(snp, "%s%-7s%s|%-7s", pfx, access_stmts[op],							  sfx, access_ops[op]);
		else
			sink_printf(snp, "%s%-7s%s", pfx, tsb2000f_ops[op], sfx);

		/* contents as operand */
		sink_printf(snp, "  ");
		if (val & 0x8000) {
			switch (type) {
			    case 0:
				sink_printf(snp, "(num)");
				break;

			    case 3:
				sink_printf(snp, "(int)");
				break;

			    default:
				if (!name) {
					/* fn param */
					sink_printf(snp, "(par)");
					break;
				}
				/* fall thru */
			    case 017:
				(void) print_other_operand(snp, val);
				break;
			}
		} else if (op == 1) {
			sink_printf(snp, "(str)");
		} else {
			if (name)
				(void) print_var_operand(snp, val);
			else if (type)
				sink_printf(snp, "(@var)");
			else
				sink_printf(snp, "     ");
		}

		/* contents as FP number */
		if (prog_nleft(&prog) >= 2) {
			sink_putc('\t', snp);
			print_number(snp, buf);
		}

		sink_putc('\n', snp);
		nused++;
	}

	sink_fini(snp);
	prog_fini(&prog);

	return NULL;
}
