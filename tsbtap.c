/*
 * Copyright 2024 Andrew B. Hastings. All rights reserved.
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <assert.h>
#include <math.h>


#define dprint(x)	if (debug) printf x

#define MIN(a, b)	((a) < (b) ? (a) : (b))
#define	LE32(bp)	((bp)[0] + ((bp)[1] << 8) + ((bp)[2] << 16) +	\
				   ((bp)[3] << 24))
#define BE16(bp)	(((bp)[0] << 8) | (bp)[1])

int debug = 0;


typedef struct {
	FILE	*tp_fp;
	size_t	tp_len;
	size_t	tp_left;
} TAPE;

TAPE *tap_open(const char *path)
{
	FILE *fp = fopen(path, "rb");
	TAPE *rv;

	if (!fp)
		return NULL;
	rv = (TAPE *)malloc(sizeof(TAPE));
	if (rv) {
		rv->tp_fp = fp;
		rv->tp_left = rv->tp_len = 0;
	}
	return rv;
}

void tap_close(TAPE *tap)
{
	fclose(tap->tp_fp);
	free(tap);
}

int tap_next(TAPE *tap)
{
	ssize_t nread;
	unsigned char buf[4];

	while (tap->tp_len) {
		if (tap->tp_left) {
			nread = fseek(tap->tp_fp, tap->tp_left, SEEK_CUR);
			if (nread < 0) {
				perror("fseek");
				exit(1);
			}
		}
		nread = fread(buf, 1, 4, tap->tp_fp);
		if (nread < 4) {
			dprint(("tap_next: EOF reading trail size at "
				"0x%lx\n", ftell(tap->tp_fp)));
			return 0;
		}
		if (tap->tp_len != LE32(buf)) {
			dprint(("tap_next: size mismatch at 0x%lx (want 0x%lx "
				"got 0x%x)\n", ftell(tap->tp_fp), tap->tp_len,
				LE32(buf)));
			return 0;
		}
		nread = fread(buf, 1, 4, tap->tp_fp);
		if (nread < 4) {
			dprint(("tap_next: EOF reading next size at "
				"0x%lx\n", ftell(tap->tp_fp)));
			return 0;
		}
		tap->tp_left = tap->tp_len = LE32(buf);
	}

	nread = fread(buf, 1, 4, tap->tp_fp);
	if (nread < 4) {
		dprint(("tap_next: EOF reading final size at 0x%lx\n",
			ftell(tap->tp_fp)));
		return 0;
	}
	tap->tp_left = tap->tp_len = LE32(buf);
	return 1;
}

ssize_t tap_read(TAPE *tap, void *buf, size_t nbyte)
{
	size_t fbytes;
	ssize_t nread;
	unsigned char sbuf[4];
	char *bp = buf;

	while (nbyte > 0) {
		if (tap->tp_len == 0)
			break;
		fbytes = MIN(nbyte, tap->tp_left);
		nread = fread(bp, 1, fbytes, tap->tp_fp);
		tap->tp_left -= nread;
		bp += nread;
		if (nread != fbytes) {
			dprint(("tap_read: EOF reading data at 0x%lx\n",
				ftell(tap->tp_fp)));
			break;
		}
		nbyte -= nread;
		if (tap->tp_left == 0) {
			nread = fread(sbuf, 1, 4, tap->tp_fp);
			if (nread < 4) {
				dprint(("tap_read: EOF reading trail size at "
					"0x%lx\n", ftell(tap->tp_fp)));
				return 0;
			}
			if (tap->tp_len != LE32(sbuf)) {
				dprint(("tap_read: size mismatch at 0x%lx "
					"(want 0x%lx got 0x%x)\n",
					ftell(tap->tp_fp), tap->tp_len,
					LE32(sbuf)));
				return 0;
			}
			nread = fread(sbuf, 1, 4, tap->tp_fp);
			if (nread < 4) {
				dprint(("tap_read: EOF reading next size at "
					"0x%lx\n", ftell(tap->tp_fp)));
				return 0;
			}
			tap->tp_left = tap->tp_len = LE32(sbuf);
		}
	}
	return bp - (char *)buf;
}


typedef struct {
	TAPE	*tk_tap;
	size_t	tk_left;
} TOKEN;

TOKEN *tok_open(TAPE *tap)
{
	TOKEN *rv = (TOKEN *)malloc(sizeof(TOKEN));

	if (rv) {
		rv->tk_tap = tap;
		rv->tk_left = 0;
	}
	return rv;
}

void tok_close(TOKEN *tok)
{
	free(tok);
}	

int tok_next(TOKEN *tok)
{
	ssize_t nbytes;
	unsigned char buf[4];

	if (debug)
		assert(tok->tk_left == 0);
	nbytes = tap_read(tok->tk_tap, buf, 4);
	if (nbytes < 4) {
		dprint(("tok_next: EOF at 0x%lx\n", ftell(tok->tk_tap->tp_fp)));
		return -1;
	}
	tok->tk_left = 2 * BE16(buf+2) - 4;
	return BE16(buf);
}

ssize_t tok_read(TOKEN *tok, void *buf, size_t nbyte)
{
	ssize_t nread;
	char *bp = buf;

	assert((nbyte & 1) == 0);
	nbyte = MIN(tok->tk_left, nbyte);
	while (nbyte > 0) {
		nread = tap_read(tok->tk_tap, bp, nbyte);
		tok->tk_left -= nread;
		bp += nread;
		if (nread != nbyte) {
			dprint(("tok_read: EOF at 0x%lx\n",
				ftell(tok->tk_tap->tp_fp)));
			break;
		}
		nbyte -= nread;
	}
	return bp - (char *)buf;
}



char *to_date(char *buf, int yr, int ydy)
{
	static char ml[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	static char mos[] = "JanFebMarAprMayJunJulAugSepOctNovDec???";
	int i;

	yr += 1900;
	ml[1] = 28 + (yr % 4 == 0);
	for (i = 0; i < 12; i++) {
		if (ydy - ml[i] < 0)
			break;
		ydy -= ml[i];
	}
	sprintf(buf, "%2d-%.3s-%4d", ydy+1, mos + 3*i, yr);
	return buf;
}

int do_topt(TAPE *tap, int vopt)
{
	unsigned char dbuf[24];
	ssize_t nread;
	int i, uid, len, olduid = -1;
	uint bits, bits2, acc;
	char name[6];
	char flags[3];
	char buf1[20];

	while (tap_next(tap)) {
		nread = tap_read(tap, dbuf, 24);
		if (nread < 18)
			continue;
		uid = BE16(dbuf);
		if (vopt &&
		    (int16_t)uid == -10 &&
		    strncmp((const char *)dbuf+2, "LBTS", 4) == 0) {
			printf("\n**DUMP %2d**  %s  lvl %d/%d\n", BE16(dbuf+8),
			       to_date(buf1, BE16(dbuf+10),
				       (BE16(dbuf+12) / 24)),
			       BE16(dbuf+16), BE16(dbuf+18));
			continue;
		}
		if (nread < 24)
			continue;
		bits = BE16(dbuf+14);
		bits2 = ((dbuf[2] & 0x80) >> 7) |
			((dbuf[4] & 0x80) >> 6) |
			((dbuf[6] & 0x80) >> 5);
		len = BE16(dbuf+22);
		if (!(bits2 & 0x2))
			len = -(int16_t) len;
		for (i = 0; i < 6; i++)
			name[i] = dbuf[i+2] & 0x7f;
		flags[0] = " AFAC???"[bits2];
		flags[1] = " L"[(bits & 0x4) != 0];
		flags[2] = " P"[(bits & 0x2) != 0];
		if (olduid != uid) {
			if (!vopt)
				printf("\n");
			printf("\n%c%03d:\n", '@' + (uid >> 10),
				uid & 0x3ff);
		}
		olduid = uid;
		if (!vopt) {
			printf("%.6s %.3s %4d\t", name, flags, len);
			continue;
		}
		acc = BE16(dbuf+10);
		printf("%.6s %.3s %4d  %s\n", name, flags, len,
		       to_date(buf1, acc >> 9, acc & 0x1ff));

	}

	if (!vopt)
		printf("\n");
	return 0;
}


double expt_tab[256];

void init_expt(void)
{
	int i;
	double a, b;

	a = 1.0 / (1 << 23);
	b = 0.5 / (1 << 23);
	for (i = 0; i < 128; i++) {
		expt_tab[2*i] = a;
		expt_tab[2*(127-i) + 1] = b;
		a *= 2;
		b /= 2;
	}
}

char cmap[256];
char pmap[256];

#define CTRL(x)	((x) & 0x1f)

void init_cmap(void)
{
	int i;

	for (i = 0; i < 256; i++)
		cmap[i] = i;
	cmap['\r'] = CTRL('O');
	cmap['\n'] = CTRL('N');
	cmap[CTRL('N')] = '\n';
	cmap[CTRL('O')] = '\r';

	for (i = 32; i < 127; i++)
		pmap[i] = 1;
	pmap['"'] = 0;
}

typedef struct {
	char	**opnames_stmt;
	char	**opnames_base;
	char	*fnames;
	int	newstring;
} lang_tab;

size_t do_line(TOKEN *tok, FILE *fp, lang_tab *lang)
{
	unsigned char tbuf[256];
	char **opnames = lang->opnames_stmt;
	char *cur_op;
	int code;
	int is_multic, is_rem, is_files, is_image, is_dim = 0;
	ssize_t nread;
	int op, nm, typ;

	code = tok_next(tok);
	if (code < 0)
		return 0;

	fprintf(fp, "%d ", code);

	while (tok_read(tok, tbuf, 2) == 2) {
		code = BE16(tbuf);
		op = (code >> 9) & 0x3f;
		nm = (code >> 4) & 0x1f;
		typ = code       & 0xf;
		cur_op = opnames[op];
		is_multic = strlen(cur_op) > 1;
		dprint(("0x%04x<%d,0%02o,0%o,0%o>@0x%lx", code,
			code >> 15, op, nm, typ, ftell(tok->tk_tap->tp_fp)));
		if (is_multic) {
			fprintf(fp, " ");
			is_dim   = strcmp(cur_op, "DIM") == 0 ||
				   strcmp(cur_op, "COM") == 0 ||
				   strcmp(cur_op, "USING") == 0;
			is_rem   = strcmp(cur_op, "REM") == 0;
			is_files = strcmp(cur_op, "FILES") == 0;
			is_image = strcmp(cur_op, "IMAGE") == 0;
		} else
			is_rem = is_files = is_image = 0;
		if (op != 1)
			fprintf(fp, "%s", cur_op);
		if (is_rem || is_files || is_image) {
			/* REM/FILES/IMAGE */
			if (is_files)
				fprintf(fp, " ");
			if (!is_image && (code & 0xff))
				fprintf(fp, "%c", code & 0xff);
			while (nread = tok_read(tok, tbuf, sizeof tbuf)) {
				if (!tbuf[nread-1])
					nread--;
				fwrite(tbuf, 1, nread, fp);
			}
			break;
		}
		if (is_multic)
			fprintf(fp, " ");
		opnames = lang->opnames_base;
		if (code >> 15) {
			/* constant operand */
			if (typ == 0) {
				int32_t mant, expt;

				if (tok_read(tok, tbuf, 4) < 4) {
					dprint(("no space for number\n"));
					return 0;
				}
				mant = (tbuf[0] << 16) |
				       (tbuf[1] << 8) |
				        tbuf[2];
				expt = tbuf[3];
				fprintf(fp, "%.7G", mant * expt_tab[expt]);
			} else if (typ == 3) {
				char *sep = "";

				while (tok_read(tok, tbuf, 2) == 2) {
					fprintf(fp, "%s%d", sep, BE16(tbuf));
					if (is_dim)
						break;
					sep = ",";
				}
			} else if (typ == 4) {
				fprintf(fp, "%c", '@' + nm);
			} else if (typ < 017) {
				fprintf(fp, "%c%d", '@' + nm, typ - 5);
			} else if (typ == 017) {
				fprintf(fp, "%.3s", lang->fnames + 3 * nm);
				if (nm == 027 || nm == 030)
					fprintf(fp, "$");
			} else {
				fprintf(fp, "?0%o", typ);
			}
		} else if (op == 1) {
			/* string operand */
			int strln = code & 0xff;
			int i;
			nread = (strln+1) & ~1;

			if (tok_read(tok, tbuf, nread) != nread) {
				dprint(("no space for string\n"));
				return 0;
			}

			if (lang->newstring) {
				int c, inquote = 0;
				for (i = 0; i < strln; i++) {
					c = tbuf[i];
					if (pmap[c]) {
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
			} else {
				putc('"', fp);
				for (i = 0; i < strln; i++) {
					putc(cmap[tbuf[i]], fp);
				}
				putc('"', fp);
			}
		} else {
			/* variable operand */
			if (nm > 032) {
				fprintf(fp, "%c%d$",
					'A' + ((code - 0xb0) & 0x1f), nm > 033);
			} else if (typ == 0) {
				if (nm)
					fprintf(fp, "%c$", '@' + nm);
			} else if (typ <= 4) {
				fprintf(fp, "%c", '@' + nm);
			} else if (typ < 017) {
				fprintf(fp, "%c%d", '@' + nm, typ - 5);
			} else if (typ == 017) {
				fprintf(fp, "FN%c", '@' + nm);
			} else {
				fprintf(fp, "?0%o", typ);
			}
		}
		dprint(("\n"));
	}
	fprintf(fp, "\n");
	return 1;
}



int do_xopt(TAPE *tap, int vopt, char **argv)
{
	static char *acc_stmt_ops[] = {
		"?00", "?01", "?02", "?03", "?04", "?05", "?06", "?07",
		"?10", "?11", "?12", "?13", "?14", "?15", "?16", "?17",
		"?20", "?21", "?22", "?23", "?24", "?25", "?26", "?27",
		"?30", "?31", "SYSTEM", "CONVERT",
		"LOCK", "UNLOCK", "CREATE", "PURGE",
		"ADVANCE", "UPDATE", "ASSIGN", "LINPUT",
		"IMAGE", "COM", "LET", "DIM",
		"DEF", "REM", "GOTO", "IF",
		"FOR", "NEXT", "GOSUB", "RETURN",
		"END", "STOP", "DATA", "INPUT",
		"READ", "PRINT", "RESTORE", "MAT"
		"FILES", "CHAIN", "ENTER", " ",
		"?74", "?75", "?76", "?77"};
	static char *acc_base_ops[] = {
		"", "\"", ",", ";", "#", "?05", "?06", "?07",
		")", "]", "[", "(", "+", "-", ",", "=",
		"+", "-", "*", "/", "^", ">", "<", "#",
		"=", "?31", "AND", "OR", "MIN", "MAX", "<>", ">=",
		"<=", "NOT", "**", "USING", "PR", "WR", "NR", "ERROR",
		"?50", "?51", "?52", "?53", "?54", "?55", "?56", "?57",
		"END", "?61", "?62", "INPUT", "READ", "PRINT", "?66", "?67",
		"?70", "?71", "?72", "?73", "OF", "THEN", "TO", "STEP" };
	static char *tsb2000c_ops[] = {
		"", "\"", ",", ";", "#", "?05", "?06", "?07",
		")", "]", "[", "(", "+", "-", ",", "=",
		"+", "-", "*", "/", "^", ">", "<", "#",
		"=", "?31", "AND", "OR", "MIN", "MAX", "<>", ">=",
		"<=", "NOT", "ASSIGN", "USING", "IMAGE", "COM", "LET", "DIM",
		"DEF", "REM", "GOTO", "IF", "FOR", "NEXT", "GOSUB", "RETURN",
		"END", "STOP", "DATA", "INPUT", "READ", "PRINT", "RESTORE", "MAT",
		"FILES", "CHAIN", "ENTER", " ", "OF", "THEN", "TO", "STEP" };
	static char fns[] = "CTLTABLINSPATANATNEXPLOGABSSQRINTRNDSGNLENTYPTIM"
			    "SINCOSBRKITMRECNUMPOSCHRUOSSYS?32ZERCONIDNINVTRN";
	static lang_tab tsb2000c_tab = {tsb2000c_ops, tsb2000c_ops, fns, 0};
	static lang_tab access_tab   = {acc_stmt_ops, acc_base_ops, fns, 1};
	unsigned char dbuf[24];
	char buf[512];
	ssize_t nread, nwrite;
	int i, uid, is_file, is_ascii, len;
	FILE *fp;
	char name[12];
	TOKEN *tok;

	init_expt();
	init_cmap();
	tok = tok_open(tap);
	while (tap_next(tap)) {
		nread = tap_read(tap, dbuf, 24);
		if (nread < 24)
			continue;
		uid = BE16(dbuf);
		sprintf(name, "%c%03d/", '@' + (uid >> 10), uid & 0x3ff);
		for (i = 0; i < 6; i++) {
			name[i+5] = dbuf[i+2] & 0x7f;
			if (name[i+5] == ' ')
				break;
		}
		if (!name[5])
			continue;
		name[i+5] = '\0';
		is_ascii = dbuf[2] & 0x80;
		is_file = dbuf[4] & 0x80;

		fp = fopen(name, is_file && !is_ascii ? "wb" : "w");
		if (!fp) {
			name[4] = '\0';
			mkdir(name, 0777);
			name[4] = '/';
			fp = fopen(name, is_file && !is_ascii ? "wb" : "w");
		}
		if (!fp) {
			perror(name);
			exit(1);
		}

		len = 0;
		if (is_file || is_ascii) {
			while (nread = tap_read(tap, buf, sizeof buf)) {
				nwrite = fwrite(buf, 1, nread, fp);
				if (nwrite != nread) {
					perror(name);
					exit(2);
				}
				len += nread;
			}
		} else {
			while (nwrite = do_line(tok, fp, &tsb2000c_tab))
				len += nwrite;
		}

		fclose(fp);
		if (vopt)
			printf("x %s %d bytes\n", name, len);
	}
	tok_close(tok);
	return 0;
}


void main(int argc, char **argv)
{
	int c;
	int err = 0, topt = 0, xopt = 0, vopt = 0;
	char *ifile = NULL;
	TAPE *tap;

	while ((c = getopt(argc, argv, "dvtxf:")) != -1) {
		switch (c) {
		    case 'd':
			debug = 1;
			break;

		    case 'v':
			vopt = 1;
			break;

		    case 't':
			topt = 1;
			break;

		    case 'x':
			xopt = 1;
			break;

		    case 'f':
			ifile = optarg;
			break;

		    case ':':
			fprintf(stderr, "option -%c requires an operand\n",
				optopt);
			err++;
			break;

		    case '?':
			fprintf(stderr, "unrecognized option -%c\n", optopt);
			err++;
			break;
		}
	}

	if (debug)
		setbuf(stdout, NULL);

	if (err || !ifile || topt && xopt || !topt && !xopt) {
		fprintf(stderr,
			"Usage: %s -f path.tap [-v] [-t | -x] [files...]\n",
			argv[0]);
		exit(1);
	}

	if (!(tap = tap_open(ifile))) {
		perror(ifile);
		exit(1);
	}

	if (topt)
		err = do_topt(tap, vopt);
	else
		err = do_xopt(tap, vopt, argv+optind);

	exit(err);
}
