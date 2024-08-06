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
	tfile_ctx_t	*st_ctx;
	int		st_nleft;	/* in bytes */
} stmt_ctx_t;


/* returns TSB line number, -1 if error */
int stmt_init(stmt_ctx_t *ctx, tfile_ctx_t *tfile)
{
	unsigned char buf[4];
	int nbytes;

	/* get line number, count of 16-bit words */
	nbytes = tfile_getbytes(tfile, buf, 4);
	if (nbytes < 4) {
		dprint(("stmt_init: EOF at 0x%lx\n",
			ftell(tfile->tfile_tap->tp_fp)));
		return -1;
	}
	ctx->st_ctx = tfile;
	ctx->st_nleft = 2 * BE16(buf+2) - 4;
	return BE16(buf);
}


/* returns number of bytes copied. nbytes must be even */
int stmt_getbytes(stmt_ctx_t *ctx, unsigned char *buf, int nbytes)
{
	int nread;

	assert((nbytes & 1) == 0);
	nbytes = MIN(ctx->st_nleft, nbytes);
	nread = tfile_getbytes(ctx->st_ctx, buf, nbytes);
	ctx->st_nleft -= nread;
	if (nread != nbytes)
		dprint(("stmt_getbytes: EOF at 0x%lx\n",
			ftell(ctx->st_ctx->tfile_tap->tp_fp)));
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
	"FILES", "CHAIN", "ENTER", " ", "?74", "?75", "?76", "?77"
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
	"FILES", "CHAIN", "ENTER", " ", "OF", "THEN", "TO", "STEP"
};


char *print_str_operand(FILE *fp, stmt_ctx_t *ctx, unsigned token)
{
	int len = token & 0xff;
	int i, nread;
	unsigned char c, tbuf[256];

	if (len == 0) {
		fprintf(fp, "\"\"");
		return NULL;
	}

	/* consume even number of bytes */
	nread = (len+1) & ~1;
	if (stmt_getbytes(ctx, tbuf, nread) != nread)
		return "string extends past end of statement";

	/* Access: use 'decimal notation for non-printable chars */
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


char *print_other_operand(FILE *fp, stmt_ctx_t *ctx, unsigned token,
			  unsigned stmt)
{
	static char fns[] = "CTLTABLINSPATANATNEXPLOGABSSQRINTRNDSGNLENTYPTIM"
			    "SINCOSBRKITMRECNUMPOSCHRUPSSYS?32ZERCONIDNINVTRN";
	unsigned char tbuf[4];
	unsigned op =   (token >> 9) & 0x3f;
	unsigned name = (token >> 4) & 0x1f;
	unsigned type =  token       & 0xf;

	switch (type) {
	    case 0:			/* floating-point number */
		if (stmt_getbytes(ctx, tbuf, 4) != 4)
			return "number extends past end of statement";
		print_number(fp, tbuf);
		break;

	    case 1: case 2:		/* not used */
		return "unknown operand type";
		break;

	    case 3:
		if (stmt_getbytes(ctx, tbuf, 2) != 2)
			return "value extends past end of statement";
		fprintf(fp, "%d", BE16(tbuf));
		if (op   == 043 ||	/* USING */
		    stmt == 045 ||	/* COM */
		    stmt == 047)	/* DIM */
			break;

					/* GOTO/GOSUB OF */
		while (stmt_getbytes(ctx, tbuf, 2) == 2)
			fprintf(fp, ",%d", BE16(tbuf));
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


char *extract_program(tfile_ctx_t *tfile, char *fn, char *oname)
{
	stmt_ctx_t ctx;
	int lineno, prev_lineno = 0;
	char *err = NULL;
	FILE *fp;

	fp = out_open(fn, "bas", oname);
	if (!fp)
		return "";

	dprint(("extract_program: %s\n", fn));

	while ((lineno = stmt_init(&ctx, tfile)) >= 0) {
		unsigned char tbuf[512];
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

		while (stmt_getbytes(&ctx, tbuf, 2) == 2) {
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
					while (nread =
						  stmt_getbytes(&ctx, tbuf,
							        sizeof tbuf)) {
						if (!tbuf[nread-1])
							nread--;
						fwrite(tbuf, 1, nread, fp);
					}
					goto next;
				}
			}

			fprintf(fp, "%s", space);
			if (token & 0x8000) {
				err = print_other_operand(fp, &ctx, token,
							  stmt);
			} else if (op == 1) {
				err = print_str_operand(fp, &ctx, token);
			} else {
				err = print_var_operand(fp, token);
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
	return err;
}
