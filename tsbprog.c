/*
 * Copyright 2024 Andrew B. Hastings. All rights reserved.
 *
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
#include "outfile.h"
#include "simtap.h"
#include "tfilefmt.h"
#include "tsbtap.h"


typedef struct {
	unsigned char	*pg_buf;
	unsigned char	*pg_bp;		/* sequential read position */
	int		pg_sz;		/* program text w/out symtab */
	int		pg_nread;	/* total read from tape */
} prog_ctx_t;


int prog_init(prog_ctx_t *prog, tfile_ctx_t *tfile)
{
#define INCR 2048		/* observed tape block size */
	unsigned char *buf;
	int rv, nread;
	int bufsz = 8 * INCR;	/* should be enough for largest TSB program */
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
		bufsz += INCR;
		if (!(buf = realloc(buf, bufsz))) {
			printf("out of memory for BASIC program\n");
			return -2;
		}
		readsz = INCR;
	}
	if (rv == -2)
		return rv;
	if (rv >= 0)
		nread += rv;

	prog->pg_buf = prog->pg_bp = buf;
	prog->pg_sz = prog->pg_nread = nread;
	dprint(("prog_init: bufsz=%d progsz=%d\n", bufsz, nread));

	return 0;
#undef INCR
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


/* read bytes sequentially up to pg_sz. not affected by prog_getbytesat */
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


typedef struct {
	prog_ctx_t	*st_ctx;
	int		st_nleft;	/* in bytes */
} stmt_ctx_t;


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


/* returns number of bytes copied. nbytes must be even */
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
	"<=", "NOT", "**", "USING", "PR", "WR", "NR", "ERROR",
	"?50", "?51", "?52", "?53", "?54", "?55", "?56", "?57",
	"END", "?61", "?62", "INPUT", "READ", "PRINT", "?66", "?67",
	"?70", "?71", "?72", "?73", "OF", "THEN", "TO", "STEP"
};
static char *tsb2000c_ops[] = {
	"", "" /* " */, ",", ";", "#", "?05", "?06", "?07",
	")", "]", "[", "(", "+", "-", ",", "=",
	"+", "-", "*", "/", "^", ">", "<", "#",
	"=", "?31", "AND", "OR", "MIN", "MAX", "<>", ">=",
	"<=", "NOT", "ASSIGN", "USING", "IMAGE", "COM", "LET", "DIM",
	"DEF", "REM", "GOTO", "IF", "FOR", "NEXT", "GOSUB", "RETURN",
	"END", "STOP", "DATA", "INPUT", "READ", "PRINT", "RESTORE", "MAT",
	"FILES", "CHAIN", "ENTER", " " /* (LET) */, "OF", "THEN", "TO", "STEP"
};


char *print_str_operand(FILE *fp, unsigned token, stmt_ctx_t *ctx)
{
	int len = token & 0xff;
	int i, nread;
	unsigned char c, *tbuf;

	if (len == 0) {
		fprintf(fp, "\"\"");
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
					putc('"', fp);
				inquote = 1;
				putc(c, fp);
			} else {
				if (inquote)
					putc('"', fp);
				inquote = 0;
				fprintf(fp, "'%d", c);
			}
		}
		if (inquote)
			putc('"', fp);

	/* pre-Access: ctrl-N for LF, ctrl-O for CR */
	} else {
		putc('"', fp);
		for (i = 0; i < len; i++) {
			switch (c = tbuf[i]) {
			    case '\n':
				c = '\016';	/* ctrl-N */
				break;

			    case '\r':
				c = '\017';	/* ctrl-O */
				break;

			    case '\016':	/* ctrl-N */
				c = '\n';
				break;

			    case '\017':	/* ctrl-O */
				c = '\r';
				break;
			}
			putc(c, fp);
		}
		putc('"', fp);
	}

	return NULL;
}


char *print_var_operand(FILE *fp, unsigned token)
{
	unsigned name = (token >> 4) & 0x1f;
	unsigned type =  token       & 0xf;

	/* string variable with digit 0 or 1 */
	if (name > 032) {
		fprintf(fp, "%c%d$", 'A' + ((token - 0xb0) & 0x1f), name > 034);
		return NULL;
	}

	switch (type) {
	    case 0:			/* string variable */
		if (!name)		/* null operand */
			break;
		fprintf(fp, "%c$", '@' + name);
		break;

	    case 1: case 2: case 3:	/* array variable */
	    case 4:			/* simple variable, no digit */
		fprintf(fp, "%c", '@' + name);
		break;

	    case 017:			/* user-defined function */
		fprintf(fp, "FN%c", '@' + name);
		break;

	    default:			/* simple variable with digit 0-9 */
		fprintf(fp, "%c%d", '@' + name, type - 5);
		break;
	}

	return NULL;
}


/* prog != NULL indicates CSAVEd */
char *print_int_operand(FILE *fp, unsigned token, unsigned stmt,
			int start, prog_ctx_t *prog, stmt_ctx_t *ctx)
{
	unsigned char *tbuf;
	char *err = NULL;
	int val;
	int is_dim = (stmt == 045 || stmt == 047);	/* COM, DIM */

	if (stmt_getbytes(ctx, &tbuf, 2) != 2)
		return "value extends past end of statement";
	val = BE16(tbuf);
	if (prog && !is_dim) {		/* if CSAVEd, get dest lineno */
		if (prog_getbytesat(prog, &tbuf, 2, (val - start) * 2) == 2)
			val = BE16(tbuf);
		else {
			dprint(("print_int_operand: offset bad 0x%04x\n", val));
			err = "corrupted destination line number";
		}
	}
	fprintf(fp, "%d", val);

	if (is_dim || ((token >> 9) & 0x3f) == 043)	/* USING */
		return err;

	while (stmt_getbytes(ctx, &tbuf, 2) == 2) {	/* GOTO/GOSUB OF */
		val = BE16(tbuf);
		if (prog && !is_dim) {	/* if CSAVEd, get dest lineno */
			if (prog_getbytesat(prog, &tbuf, 2, (val - start) * 2)
									  == 2)
				val = BE16(tbuf);
			else {
				dprint(("print_int_operand: bad offset "
					"0x%04x\n", val));
				err = "corrupted destination line number";
			}
		}
		fprintf(fp, ",%d", val);
	}

	return err;
}


char *print_other_operand(FILE *fp, unsigned token)
{
	static char fns[] = "CTLTABLINSPATANATNEXPLOGABSSQRINTRNDSGNLENTYPTIM"
			    "SINCOSBRKITMRECNUMPOSCHRUPSSYS?32ZERCONIDNINVTRN";
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
		fprintf(fp, "%c", '@' + name);
		break;

	    case 017:			/* built-in function */
		fprintf(fp, "%.3s", fns + 3 * name);
		if (name == 027 || name == 030)
			fprintf(fp, "$");
		break;

	    default:			/* formal param with digit 0-9 */
		fprintf(fp, "%c%d", '@' + name, type - 5);
		break;
	}

	return NULL;
}


char *extract_program(tfile_ctx_t *tfile, char *fn, char *oname,
		      unsigned char *dbuf)
{
	prog_ctx_t prog;
	stmt_ctx_t ctx;
	int len = 2 * -(int16_t) BE16(dbuf+22);
	int lineno, prev_lineno = 0;
	int symtab = 0;			/* offset in bytes */
	int start = BE16(dbuf+8);	/* 16-bit words */
	char *err = NULL;
	FILE *fp;

	dprint(("extract_program: %s\n", fn));

	if (prog_init(&prog, tfile) < 0)
		return "";

	if (dbuf[6] & 0x80) {	/* CSAVEd */
		unsigned char *buf;

		if (prog_getbytesat(&prog, &buf, 2, len - 12) == 2)
			symtab = (BE16(buf) - start) * 2;
		else
			err = "can't find symtab for CSAVEd program";
		if (symtab <= 0)
			err = "invalid symtab addr for CSAVEd program";
		if (err) {
			prog_fini(&prog);
			return err;
		}
		prog_setsz(&prog, symtab);
	} else
		prog_setsz(&prog, len);

	fp = out_open(fn, "bas", oname);
	if (!fp) {
		prog_fini(&prog);
		return "";
	}

	while ((lineno = stmt_init(&ctx, &prog)) >= 0) {
		unsigned char *tbuf;
		unsigned stmt = 0;
		int nread;
		char **opnames = is_access > 0 ? access_stmts : tsb2000c_ops;

		dprint(("extract_program: line %d\n", lineno));
		if (lineno > 9999 || lineno <= prev_lineno) {
			err = "lines out of order";
			stmt_fini(&ctx);
			break;
		}
		fprintf(fp, "%d ", lineno);
		prev_lineno = lineno;

		while (stmt_getbytes(&ctx, &tbuf, 2) == 2) {
			unsigned token = BE16(tbuf);
			unsigned op = (token >> 9) & 0x3f;
			char *space, *name = opnames[op];

			dprint(("extract_program: 0x%04x <%d,0%02o,0%o,0%o> "
				"@ 0x%lx\n", token, token >> 15, op,
				(token >> 4) & 0x1f, token & 0xf,
				ftell(tfile->tfile_tap->tp_fp)));
			space = name[0] && name[1] ? " " : "";
			fprintf(fp, "%s%s", space, name);

			/* save statement code; process special cases */
			if (opnames != access_ops) {
				stmt = op;
				switch (op) {
				    case 070:	/* FILES */
					putc(' ', fp);
					/* fall thru */
				    case 051:	/* REM */
					if (token & 0xff)
						putc(token & 0xff, fp);
					/* fall thru */
				    case 044:	/* IMAGE */
					while (nread = stmt_getbytes(&ctx,
								&tbuf, 256)) {
						if (!tbuf[nread-1])
							nread--;
						fwrite(tbuf, 1, nread, fp);
					}
					goto next;
				}
			}

			fprintf(fp, "%s", space);
			if (token & 0x8000) {
				unsigned type = token & 0xf;

				if (type == 0) {	/* FP number */
					if (stmt_getbytes(&ctx, &tbuf, 4) != 4)
						return "number extends past "
						       "end of statement";
					print_number(fp, tbuf);

				} else if (type == 3)	/* line # or DIM */
					err = print_int_operand(fp,
							token, stmt, start,
						symtab ? &prog : NULL, &ctx);

				 else
					err = print_other_operand(fp, token);

			} else if (op == 1) {
				err = print_str_operand(fp, token, &ctx);

			} else {
				int idx = (token & 0x1ff);

				/* if CSAVEd, get var name from symtab */
				if (symtab && idx) {
					if (prog_getbytesat(&prog, &tbuf, 2,
							symtab + 4 * (idx-1))
									== 2)
						token = BE16(tbuf);
					else
						err = "corrupted symbol table";
				}
				(void) print_var_operand(fp, token);
			}
next:
			/* Access: subsequent operators aren't stmt codes */
			if (is_access > 0)
				opnames = access_ops;

			if (err)
				break;
		}

		fprintf(fp, "\n");
		stmt_fini(&ctx);
		if (err)
			break;
	}

	out_close(fp);
	prog_fini(&prog);

	return err;
}


char *dump_program(tfile_ctx_t *tfile, char *fn, unsigned char *dbuf)
{
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

	/* replace some op names for clarity */
	for (i = 0; i < 0100; i++) {
		if (tsb2000c_ops[i][0] == '?')
			tsb2000c_ops[i] = "";
		if (access_ops[i][0] == '?')
			access_ops[i] = "";
		if (access_stmts[i][0] == '?')
			access_stmts[i] = "";
	}
	access_ops[0] = tsb2000c_ops[0] = "(end)";	/* end of formula */
	access_ops[1] = tsb2000c_ops[1] = "\"";
	access_ops[4] = tsb2000c_ops[4] = "#(file)";
	access_stmts[073] = tsb2000c_ops[073] = "(LET)";

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
		printf("%s ", pfx);

		/* offset */
		if (off & 0x7)
			printf("     ");
		else
			printf("%5x", off);

		/* contents as hex and decimal */
		pfx = sfx = "";
		if (nused == 0) {	    /* underline line number */
			pfx = "\033[4m";
			sfx = "\033[0m";
		}
		printf("  %04x (%s%5d%s)  ", val, pfx, val, sfx);

		/* contents as ASCII */
		for (i = 0; i < 2; i++) {
			unsigned char c = buf[i];

			if (c < 32 || c >= 127)
				c = '.';
			printf("%c%s", c, sfx);
		}

		/* contents as token codes */
		printf("  %d-%2o-%2o-%2o  ", val >> 15, op, name, type);

		/* contents as operator name(s) */
		pfx = sfx = "";
		if (nused == 2) {	    /* underline statement name */
			pfx = "\033[4m";
			sfx = "\033[0m";
		}
		if (is_access > 0)
			printf("%s%-7s%s|%-7s", pfx, access_stmts[op], sfx,
						access_ops[op]);
		else
			printf("%s%-7s%s", pfx, tsb2000c_ops[op], sfx);

		/* contents as operand */
		printf("  ");
		if (val & 0x8000) {
			switch (type) {
			    case 0:
				printf("(num)");
				break;

			    case 3:
				printf("(int)");
				break;

			    default:
				if (!name) {
					printf("(par)");  /* fn param */
					break;
				}
				/* fall thru */
			    case 017:
				(void) print_other_operand(stdout, val);
				break;
			}
		} else if (op == 1) {
			printf("(str)");
		} else {
			if (name)
				(void) print_var_operand(stdout, val);
			else if (type)
				printf("(var)");
			else
				printf("     ");
		}

		/* contents as FP number */
		if (prog_nleft(&prog) >= 2) {
			printf("\t");
			print_number(stdout, buf);
		}

		printf("\n");
		nused++;
	}

	prog_fini(&prog);

	return NULL;
}
