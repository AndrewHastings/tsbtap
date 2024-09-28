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
 * Read HP2000 TSB dump tapes in SIMH tape image format.
 */

#include <inttypes.h>
#include <math.h>
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
#include "tsbfile.h"
#include "tsbprog.h"
#include "tsbtap.h"


#define ACCESS_OSLVL	5000	/* TBD: find actual value */

int is_access = -1;
int ignore_errs = 0;
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
 * -d: show tokens of TSB program.
 */

int is_tsb_label(unsigned char *tbuf, int nbytes)
{
	if (nbytes >= 20 &&			/* long enough? */
	    (tbuf[0] >> 2) > 26 &&		/* not a valid id (> Z)? */
	    memcmp(tbuf+2, "LBTS", 4) == 0) {	/* name as expected? */
		if (is_access < 0)
			is_access = BE16(tbuf+16) >= ACCESS_OSLVL;
		return 1;
	}

	return 0;
}


int do_dopt(TAPE *tap, int argc, char **argv)
{
	int i, ec = 0;
	ssize_t nread;
	unsigned char *tbuf;
	char *found;

	found = alloca(argc);
	if (!found) {
		fprintf(stderr, "too many file names\n");
		return 3;
	}
	memset(found, 0, argc);

	while ((nread = tap_readblock(tap, (char **)&tbuf)) >= 0) {
		unsigned char dbuf[24];
		char *err, *fn;
		char nbuf[12], name[7];
		unsigned uid;
		int nbytes;
		tfile_ctx_t tfile;

		/* skip tapemarks */
		if (nread == 0)
			continue;

		/* skip TSB labels */
		if (is_tsb_label(tbuf, nread)) {
			tfile_ctx_init(&tfile, tap, tbuf, nread, 0);
			goto next;
		}

		tfile_ctx_init(&tfile, tap, tbuf, nread, is_access > 0 ? 0 : 2);
		nbytes = tfile_getbytes(&tfile, dbuf, 24);
		if (nbytes < 24)	/* skip short block */
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

		/* matched */
		if (dbuf[4] & 0x80)
			printf("Not dumping %s/%s\n", nbuf, name);
		else
			err = dump_program(&tfile, fn, dbuf);

		if (err) {
			ec = 2;
			if (err[0])
				printf("%s: %s\n", fn, err);
		}

next:
		tfile_skipf(&tfile);
		tfile_ctx_fini(&tfile);
	}

	if (nread == -2)
		ec = 2;

	for (i = 0; i < argc; i++)
		if (!found[i]) {
			fprintf(stderr, "%s not found\n", argv[i]);
			ec = 3;
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


char *device_str(char *buf, unsigned char *dbuf)
{
	unsigned device = BE16(dbuf+18);

	sprintf(buf, "%c%c%d", 'A' + (device >> 10),
			       'A' + ((device >> 5) & 0x1f), device & 0x1f);
	return buf;
}


void print_direntry(unsigned char *dbuf)
{
	int i, len;
	unsigned flags;
	char name[6], dev[5];
	char type, mode, sanct;

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

	printf("%.6s %c%c%c", name, type, mode, sanct);
	if (verbose || type != 'A' || len)
		printf("%4d", len);
	else
		printf("%4s", device_str(dev, dbuf));

	if (verbose) {
		unsigned adate = BE16(dbuf+10);

		printf("  ");
		print_date(adate >> 9, adate & 0x1ff);

		if (verbose > 1)
			printf(" flags=0x%04x", flags);

		if (dbuf[4] & 0x80)
			printf(" recsz=%d", BE16(dbuf+8));
		if (type == 'A' && BE16(dbuf+16) == 0xffff)
			printf(" device=%s", device_str(dev, dbuf));

		if (is_access > 0) {
			if (flags & 0x800)
				printf(" FCP");
			if (flags & 0x2000)
				printf(" PFA");
		}
	}
}


int do_topt(TAPE *tap)
{
	unsigned char *tbuf;
	ssize_t nread;
	tfile_ctx_t tfile;
	int is_hib = 0;
	int prev_uid = -1;
	int ec = 0;

	while (1) {
		int off = 0;

		nread = tap_readblock(tap, (char **) &tbuf);
		if (nread < 0) {
			if (nread == -2)
				ec = 2;
			break;
		}
		if (nread == 0) {
			printf("  --mark--\n");
			continue;
		}

		/* process TSB labels */
		tfile_ctx_init(&tfile, tap, tbuf, nread, 0);
		if (is_tsb_label(tbuf, nread)) {
			unsigned char dbuf[20];

			/* save label */
			memcpy(dbuf, tbuf, 20);

			/* hibernate tape has blocks between label & tapemark */
			if (!is_hib) {
				nread = tap_readblock(tap, (char **) &tbuf);
				if (nread > 0)
					is_hib = 1;
			}

			/* print label */
			printf("\nTSB %s reel %-2d  ",
				is_hib ? "Hibernate" : "Dump", BE16(dbuf+8));
		        print_date(BE16(dbuf+10), BE16(dbuf+12) / 24);
			printf("  oslvl %d-%d\n", BE16(dbuf+16), BE16(dbuf+18));

			if (nread < 0) {
				if (nread == -2)
					ec = 2;
				break;
			}
			if (nread == 0)
				continue;

			goto next;
		}

		if (is_access <= 0)	/* skip pre-Access header */
			off = 2;

		if (nread >= 24 + off) {
			int uid = BE16(tbuf + off);

			if (uid != prev_uid) {
				if (!verbose)
					printf("\n");
				printf("\n%c%03d:\n",
				       '@' + (uid >> 10), uid & 0x3ff);
				prev_uid = uid;
			}
			print_direntry(tbuf + off);
			printf("%s", verbose ? "\n" : "\t");

		} else {
			printf("  --short block: %ld byte%s--\n", nread,
			       nread == 1 ? "" : "s");
		}

next:
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


void print_number(FILE *fp, unsigned char *buf)
{
	char sbuf[32], *sp = sbuf;
	int e;
	double val;
	int mant, expt;

	mant = (buf[0] << 16) | (buf[1] << 8) | buf[2];
	if (buf[0] & 0x80)	/* negative: sign-extend */
		mant |= 0xff000000;
	val = mant / (1.0 * (1 << 23));

	expt = buf[3] >> 1;
	if (buf[3] & 1)		/* negative exponent */
		val /= pow(2, 128-expt);
	else
		val *= pow(2, expt);

	/* Convert to string, advance past leading '-' */
	sprintf(sp, "%G", val);
	if (*sp == '-')
		putc(*sp++, fp);

	/* Special case "0.*" */
	if (sp[0] == '0' && sp[1] == '.') {
		sp++;			/* drop initial '0' before decimal */
		if (strlen(sp+1) > 6) {	/* too many digits? */
			/* print as 'E' format */
			e = 1;
			for (sp++; *sp == '0'; sp++)
				e++;
			putc(*sp++, fp);
			fprintf(fp, ".%sE-%02d", sp, e);
			*sp = '\0';	/* nothing else to print */

		}

	/* Special case "?E*" */
	} else if (sp[1] == 'E') {
		/* small negative exp: print as decimal, not 'E' format */
		if (sp[2] == '-' && sp[3] == '0' && sp[4] < '7') {
			putc('.', fp);
			for (e = sp[4]-'0'; e > 1; e--)
				putc('0', fp);
			sp[1] = '\0';	/* delete "E*" */

		/* otherwise, insert '.' between initial digit and 'E' */
		} else {
			putc(*sp++, fp);
			putc('.', fp);
		}
	}

	/* Print remaining part of conversion to string */
	fprintf(fp, "%s", sp);

	/* Append '.' to some large integers */
	if (!strchr(sp, '.')) {
		val = fabs(val);
		if (val > 32767 && val < 1000000)
			putc('.', fp);
	}
}


int do_xopt(TAPE *tap, int argc, char **argv)
{
	int i, ec = 0;
	ssize_t nread;
	unsigned char *tbuf;
	char *found;

	found = alloca(argc);
	if (!found) {
		fprintf(stderr, "too many file names\n");
		return 3;
	}
	memset(found, 0, argc);

	while ((nread = tap_readblock(tap, (char **)&tbuf)) >= 0) {
		unsigned char dbuf[24];
		char *err, *fn;
		char nbuf[12], name[7], oname[28];
		unsigned uid;
		int nbytes;
		tfile_ctx_t tfile;

		/* skip tapemarks */
		if (nread == 0)
			continue;

		/* skip TSB labels */
		if (is_tsb_label(tbuf, nread)) {
			tfile_ctx_init(&tfile, tap, tbuf, nread, 0);
			goto next;
		}

		tfile_ctx_init(&tfile, tap, tbuf, nread, is_access > 0 ? 0 : 2);
		nbytes = tfile_getbytes(&tfile, dbuf, 24);
		if (nbytes < 24)	/* skip short block */
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

		/* place in subdir if user didn't specify id */
		if (!strchr(argv[i], '/')) {
			strcat(nbuf, "/");
			strcat(nbuf, fn);
			fn = nbuf;
		}

		/* extract file */
		if (is_access > 0 && (dbuf[2] & 0x80))
			err = extract_ascii_file(&tfile, fn, oname, dbuf);
		else if (dbuf[4] & 0x80)
			err = extract_basic_file(&tfile, fn, oname, dbuf);
		else
			err = extract_program(&tfile, fn, oname, dbuf);

		/* get access time from directory entry */
		if (oname[0]) {
			struct tm tm;
			unsigned adate = BE16(dbuf+10);

			if (jdate_to_tm(adate >> 9, adate & 0x1ff, &tm) >= 0)
				set_mtime(oname, &tm);
		}

		if (err) {
			ec = 2;
			if (err[0])
				printf("%s: %s\n", fn, err);
		}

next:
		tfile_skipf(&tfile);
		tfile_ctx_fini(&tfile);
	}

	if (nread == -2)
		ec = 2;

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
	fprintf(stderr, "Usage: %s [-aeOv] -f path.tap [-r | -t | -d files... | -x files...]\n",
		prog);
	fprintf(stderr, " -f   file in SIMH tape format (required)\n");
	fprintf(stderr, "operations:\n");
	fprintf(stderr, " -d   show tokens of TSB program \n");
	fprintf(stderr, " -r   show raw tape block structure\n");
	fprintf(stderr, " -t   catalog the tape\n");
	fprintf(stderr, " -x   extract files from tape\n");
	fprintf(stderr, "modifiers:\n");
	fprintf(stderr, " -a   ACCESS system tape (default no, or from OS level if found on tape)\n");
	fprintf(stderr, " -e   ignore certain errors when extracting\n");
	fprintf(stderr, " -O   extract to stdout (default write to file)\n");
	fprintf(stderr, " -v   verbose output\n");
	fprintf(stderr, " -vv  more verbose output\n");
	exit(ec);
}


#define OP_R	1
#define OP_T	2
#define OP_X	4
#define OP_D	8

void main(int argc, char **argv)
{
	int c, ec;
	unsigned op = 0;
	char *ifile = NULL;
	TAPE *tap;

	prog = strrchr(argv[0], '/');
	prog = prog ? prog+1 : argv[0];

	while ((c = getopt(argc, argv, "aDdef:Ohrtvx")) != -1) {
		switch (c) {
		    case 'a':
			is_access = 1;
			break;

		    case 'D':
			debug++;
			break;

		    case 'd':
			op |= OP_D;
			break;

		    case 'e':
			ignore_errs++;
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

	    case OP_D:
	    case OP_X:
		if (optind >= argc) {
			fprintf(stderr, "no files specified\n");
			usage(1);
		}
		break;

	    default:
		fprintf(stderr,
			"must specify exactly one of -d, -r, -t, or -x\n");
		usage(1);
	}


	if (debug)
		setbuf(stdout, NULL);

	if (!(tap = tap_open(ifile, 0))) {
		perror(ifile);
		exit(1);
	}

	switch (op) {
	    case OP_D:  ec = do_dopt(tap, argc-optind, argv+optind); break;
	    case OP_R:  ec = do_ropt(tap); break;
	    case OP_T:  ec = do_topt(tap); break;
	    case OP_X:  ec = do_xopt(tap, argc-optind, argv+optind); break;
	}

	tap_close(tap);

	exit(ec);
}
