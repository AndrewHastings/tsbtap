/*
 * Copyright 2024 Andrew B. Hastings. All rights reserved.
 *
 */

/*
 * Read HP2000 TSB dump tapes in SIMH tape image format.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include <math.h>
#include "outfile.h"
#include "simtap.h"
#include "tfilefmt.h"
#include "tsbtap.h"


#define ACCESS_OSLVL	5000	/* TBD: find actual value */
#define BE16(bp)	(((bp)[0] << 8) | (bp)[1])

int is_access = -1;
int debug = 0;
int verbose = 0;


/*
 * -r: show raw tape block structure.
 */

int do_ropt(TAPE *tap)
{
	ssize_t nbytes;
	int i, j, lim, ec = 0;
	unsigned char *tbuf, c;
	char *sfx;

	while (1) {
		nbytes = tap_readblock(tap, (char **) &tbuf);
		if (nbytes < 0) {
			if (nbytes == -2)
				ec = 2;
			break;
		}
		if (nbytes == 0) {
			printf("  --mark--\n");
			continue;
		}

		switch (verbose) {
		    case 0:   lim = 32; break;
		    case 1:   lim = 128; break;
		    default:  lim = nbytes; break;
		}
		lim = MIN(nbytes, lim);

		printf("%6ld  ", nbytes);
		for (i = 0; i < lim; i += 16) {
			if (i)
				printf("        ");

			/* print 16 bytes as hex */
			for (j = 0; j < 16; j++) {
				if (i+j < lim)
					printf("%02x", tbuf[i+j]);
				else
					printf("  ");
				if (j % 2 == 1)
					putchar(' ');
				if (j % 8 == 7)
					putchar(' ');
			}

			/* print 16 bytes as ASCII */
			for (j = 0; j < 16; j++) {
				if (i+j < lim) {
					sfx = "";
					c = tbuf[i+j];
					if ((c & 0x7f) < 32 ||
					    (c & 0x7f) == 127)
						c = '.';
					if (c & 0x80) {
						c &= 0x7f;
						if (c == ' ' ||
						    c >= 'A' && c <= 'Z' ||
						    c >= '0' && c <= '9') {
							/* ul */
							printf("\033[4m");
							sfx = "\033[0m";
						} else
							c = '.';
					}
					printf("%c%s", c, sfx);
				} else
					putchar(' ');
				if (j % 8 == 7)
					putchar(' ');
			}

			if (i % 64 == 0)
				printf(" 0x%x", i);
			putchar('\n');
		}
	}

	return ec;
}


/*
 * -t: catalog the tape.
 */

void print_date(int yr, int jday)
{
	static char mos[] = "JanFebMarAprMayJunJulAugSepOctNovDec???";
	struct tm tm;

	if (jdate_to_tm(yr, jday, &tm) < 0) {
		printf("??-????-%4d", yr+1900);
		return;
	}
	printf("%2d-%.3s-%4d", tm.tm_mday, mos + 3*tm.tm_mon, tm.tm_year+1900);
}


int print_direntry(unsigned char *dbuf, int prev_uid)
{
	int i, len, uid;
	unsigned flags;
	char name[6];
	char type, mode, sanct;

	uid = BE16(dbuf);
	for (i = 0; i < 6; i++)
		name[i] = dbuf[i+2] & 0x7f;
	flags = BE16(dbuf+14);		/* pre-Access: drum addr */
		    /* Access: 0=unrestricted 1=protected 2=locked 3=private */
		    /*   bits:       11=fcp 12=mwa 13=pfa 14=output 15=input */
	len = BE16(dbuf+22);

	type = mode = sanct = ' ';
	if (dbuf[4] & 0x80) {
		type = 'F';		/* TSB file */
	} else if (dbuf[6] & 0x80)
		type = 'C';		/* CSAVEd */
	if (type != 'F')
		len = -(int16_t) len;

	if (is_access > 0) {	/* Access: */
		if (dbuf[2] & 0x80)
			type = 'A';	/* ASCII file */
		if (type == 'F' && (flags & 0x1000))
			type = 'M';	/* MWA */
		if (flags & 0x1)
			mode = 'U';	/* unrestricted */
		else if (flags & 0x2)
			mode = 'P';	/* protected */
		else if (flags & 0x4)
			mode = 'L';	/* locked */
	} else {		/* pre-Access: */
		if (dbuf[2] & 0x80)
			mode = 'P';	/* protected */
		if (flags)
			sanct = 'S';	/* sanctified */
	}

	if (uid != prev_uid) {
		if (!verbose)
			printf("\n");
		printf("\n%c%03d:\n", '@' + (uid >> 10), uid & 0x3ff);
	}

	printf("%.6s %c%c%c %4d", name, type, mode, sanct, len);
	if (verbose) {
		unsigned adate = BE16(dbuf+10);
		printf("  ");
		print_date(adate >> 9, adate & 0x1ff);
		if (verbose > 1)
			printf(" flags=0x%04x", flags);
		if (dbuf[4] & 0x80)
			printf(" recsz=%d", BE16(dbuf+8));
		if (flags & 0x800)
			printf(" FCP");
		if (flags & 0x2000)
			printf(" PFA");
		printf("\n");
	} else
		printf("\t");

	return uid;
}


int do_topt(TAPE *tap)
{
	unsigned char *dbuf;
	ssize_t nread;
	tfile_ctx_t tfile;
	int uid = -1;
	int ec = 0;

	while (1) {
		nread = tap_readblock(tap, (char **) &dbuf);
		if (nread < 0) {
			if (nread == -2)
				ec = 2;
			break;
		}
		if (nread == 0) {
			printf("  --mark--\n");
			continue;
		}

		tfile_ctx_init(&tfile, tap, dbuf, nread);

		if (nread >= 18 && memcmp(dbuf, "\377\366LBTS", 6) == 0) {
			unsigned level = BE16(dbuf+16);

			printf("\nTSB Dump reel %-2d  ", BE16(dbuf+8));
		        print_date(BE16(dbuf+10), BE16(dbuf+12) / 24);
		        printf("  oslvl %d-%d\n", level, BE16(dbuf+18));
			if (is_access < 0)
				is_access = level >= ACCESS_OSLVL;

		} else if (nread >= 24)
			uid = print_direntry(dbuf, uid);

		else
			printf("Unrecognized tape block\n");

		tfile_skipf(&tfile);
		tfile_ctx_fini(&tfile);
	}

	if (!verbose)
		printf("\n");
	return ec;
}


/*
 * -x: extract files from tape.
 */


char *extract_ascii_file(tfile_ctx_t *tfile, char *fn, char *oname)
{
	return "not extracting ASCII file";
}


char *extract_basic_file(tfile_ctx_t *tfile, char *fn, char *oname)
{
#if 0
		if (is_file || is_ascii) {
			while (nread = tap_read(tap, buf, sizeof buf)) {
				nwrite = fwrite(buf, 1, nread, fp);
				if (nwrite != nread) {
					perror(name);
					exit(2);
				}
				len += nread;
			}
		}
#endif
	return "not extracting BASIC-formatted file";
}


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
	double val;
	int expt;
	unsigned op =   (token >> 9) & 0x3f;
	unsigned name = (token >> 4) & 0x1f;
	unsigned type =  token       & 0xf;

	switch (type) {
	    case 0:			/* floating-point number */
		if (stmt_getbytes(ctx, tbuf, 4) != 4)
			return "number extends past end of statement";
		val = (((tbuf[0] & 0x7f) << 16) | (tbuf[1] << 8) | tbuf[2])
							/ (1.0 * (1 << 23));
		if (tbuf[0] & 0x80)
			val = -val;
		expt = tbuf[3] >> 1;
		if (tbuf[3] & 1)
			val /= 1 << (128-expt);
		else
			val *= 1 << expt;
		fprintf(fp, "%G", val);
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
	int lineno;
	char *err = NULL;
	FILE *fp;

	fp = out_open(fn, "bas", oname);
	if (!fp)
		return "";

	while ((lineno = stmt_init(&ctx, tfile)) >= 0) {
		unsigned char tbuf[256];
		unsigned stmt = 0;
		int nread;
		char **opnames = is_access > 0 ? access_stmts : tsb2000c_ops;

		dprint(("extract_program: line %d\n", lineno));
		fprintf(fp, "%d ", lineno);

		while (stmt_getbytes(&ctx, tbuf, 2) == 2) {
			unsigned token = BE16(tbuf);
			unsigned op = (token >> 9) & 0x3f;
			char *space, *name = opnames[op];

			dprint(("extract_program: 0x%04x <%d,0%02o,0%o,0%o> @ 0x%lx\n",
				token, token >> 15, op,
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


int do_xopt(TAPE *tap, int argc, char **argv)
{
	int ec = 0;
	ssize_t nread;
	tfile_ctx_t tfile;
	char *err, *fn;
	unsigned char *tbuf;
	unsigned char dbuf[24];
	char nbuf[12], name[7], oname[28];
	int i, nbytes;
	unsigned adate, uid;
	struct tm tm;
	char *found;

	found = alloca(argc);
	if (!found) {
		fprintf(stderr, "too many file names\n");
		return 3;
	}
	memset(found, 0, argc);

	while (1) {
		nread = tap_readblock(tap, (char **)&tbuf);
		if (nread < 0) {
			if (nread == -2)
				ec = 2;
			break;
		}

		tfile_ctx_init(&tfile, tap, tbuf, nread);
		nbytes = tfile_getbytes(&tfile, dbuf, 24);

		/* skip TSB labels and short blocks */
		if (nbytes >= 18 && memcmp(dbuf, "\377\366LBTS", 6) == 0) {
			if (is_access < 0)
				is_access = BE16(dbuf+16) >= ACCESS_OSLVL;
			nbytes = 18;
		}
		if (nbytes < 24)
			goto next;

		/* get id and name */
		uid = BE16(dbuf);
		sprintf(nbuf, "%c%03d", '@' + (uid >> 10), uid & 0x3ff);
		for (i = 0; i < 6; i++) {
			name[i] = dbuf[i+2] & 0x7f;
			if (name[i] == ' ')
				break;
		}
		name[i] = '\0';

		/* check for match */
		for (i = 0; i < argc; i++)
			if (fn = name_match(argv[i], nbuf, name))
				break;
		if (i == argc) /* no match */
			goto next;
		found[i] = 1;
		err = NULL;
		oname[0] = '\0';

		/* place in subdir if user did not specify id */
		if (!strchr(argv[i], '/')) {
			strcat(nbuf, "/");
			strcat(nbuf, fn);
			fn = nbuf;
		}

		/* write file */
		if (is_access > 0 && (dbuf[2] & 0x80))
			err = extract_ascii_file(&tfile, fn, oname);
		else if (dbuf[4] & 0x80)
			err = extract_basic_file(&tfile, fn, oname);
		else if (dbuf[6] & 0x80)
			err = "not extracting CSAVEd program";
		else
			err = extract_program(&tfile, fn, oname);

		/* get access time from directory entry */
		if (oname[0]) {
			adate = BE16(dbuf+10);
			if (jdate_to_tm(adate >> 9, adate & 0x1ff, &tm) >= 0)
				set_mtime(oname, &tm);
		}

		if (err) {
			ec = 2;
			if (err[0])
				fprintf(stderr, "%s: %s\n", fn, err);
		}

next:
		tfile_skipf(&tfile);
		tfile_ctx_fini(&tfile);
	}

	for (i = 0; i < argc; i++)
		if (!found[i]) {
			fprintf(stderr, "%s not found\n", argv[i]);
			ec = 3;
		}
	return ec;
}


/*
 * Main program.
 */

char *prog;


void usage(int ec)
{
	fprintf(stderr, "Usage: %s [-aOv] -f path.tap [-r | -t | -x files...]\n",
		prog);
	fprintf(stderr, " -f   file in SIMH tape format (required)\n");
	fprintf(stderr, "operations:\n");
	fprintf(stderr, " -r   show raw tape block structure\n");
	fprintf(stderr, " -t   catalog the tape\n");
	fprintf(stderr, " -x   extract files from tape\n");
	fprintf(stderr, "modifiers:\n");
	fprintf(stderr, " -a   ACCESS system tape (default no, or from OS level if found on tape)\n");
	fprintf(stderr, " -O   extract to stdout (default write to file)\n");
	fprintf(stderr, " -v   verbose output\n");
	fprintf(stderr, " -vv  more verbose output\n");
	exit(ec);
}


#define OP_R	1
#define OP_T	2
#define OP_X	4

void main(int argc, char **argv)
{
	int c, ec;
	unsigned op = 0;
	char *ifile = NULL;
	TAPE *tap;

	prog = strrchr(argv[0], '/');
	prog = prog ? prog+1 : argv[0];

	while ((c = getopt(argc, argv, "adf:Ohrtvx")) != -1) {
		switch (c) {
		    case 'a':
			is_access = 1;
			break;

		    case 'd':
			debug++;
			break;

		    case 'f':
			ifile = optarg;
			break;

		    case 'h':
			usage(0);
			break;

		    case 'O':
			sout++;
			break;

		    case 'r':
			op |= OP_R;
			break;

		    case 't':
			op |= OP_T;
			break;

		    case 'v':
			verbose++;
			break;

		    case 'x':
			op |= OP_X;
			break;

		    case ':':
			fprintf(stderr, "option -%c requires an operand\n",
				optopt);
			usage(1);
			break;

		    case '?':
			fprintf(stderr, "unrecognized option -%c\n", optopt);
			usage(1);
			break;
		}
	}

	if (!ifile) {
		fprintf(stderr, "-f must be specified\n");
		usage(1);
	}

	switch (op) {
	    case OP_R:
	    case OP_T:
		if (optind < argc) {
			fprintf(stderr, "files not allowed with -%c\n",
				op == OP_R ? 'r' : 't');
			usage(1);
		}
		break;

	    case OP_X:
		if (optind >= argc) {
			fprintf(stderr, "no files specified\n");
			usage(1);
		}
		break;

	    default:
		fprintf(stderr,
			"must specify exactly one of -r, -t, or -x\n");
		usage(1);
	}


	if (debug)
		setbuf(stdout, NULL);

	if (!(tap = tap_open(ifile))) {
		perror(ifile);
		exit(1);
	}

	switch (op) {
	    case OP_R:  ec = do_ropt(tap); break;
	    case OP_T:  ec = do_topt(tap); break;
	    case OP_X:  ec = do_xopt(tap, argc-optind, argv+optind); break;
	}

	tap_close(tap);

	exit(ec);
}
