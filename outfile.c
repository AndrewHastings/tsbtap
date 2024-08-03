/*
 * Copyright 2024 Andrew B. Hastings. All rights reserved.
 *
 */

/*
 * Output file utility routines.
 */

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#undef _POSIX_C_SOURCE  /* I didn't set it; who did?? */
#include <fnmatch.h>
#include "tsbtap.h"
#include "outfile.h"

int sout = 0;


/* match pattern as "id/pat" or "pat" */
/* returns pat if case-free exact match, name if wildcard match, or NULL */
char *name_match(char *pattern, char *id, char *name)
{
	char *pat, *sp;

	pat = pattern;
	sp = strchr(pattern, '/');

	/* id specified */
	if (sp) {
		if (strncasecmp(id, pattern, sp - pattern) != 0)
			return NULL;
		pat = sp+1;
	}

	dprint(("name_match: pat=%s\n", pat));
	if (strcasecmp(pat, name) == 0)
		return pat;
	if (fnmatch(pat, name, FNM_CASEFOLD) == 0)
		return name;
	return NULL;
}


/* convert Julian date into *tm */
/* returns -1 if failure */
int jdate_to_tm(int yr, int jday, struct tm *tm)
{
	static char days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int i;

	days[1] = 28 + (yr % 4 == 0);
	for (i = 0; i < 12; i++) {
		if (jday - days[i] <= 0)
			break;
		jday -= days[i];
	}
	if (i == 12)
		return -1;

	tm->tm_year = yr;
	tm->tm_mon = i;
	tm->tm_mday = jday;
	return 0;
}


void set_mtime(char *fname, struct tm *tm)
{
	struct timeval times[2];

	if (!fname[0])
		return;

	tm->tm_isdst = -1;
	times[1].tv_sec = mktime(tm);
	if (times[1].tv_sec == -1) {
		fprintf(stderr, "%s: mtime invalid\n", fname);
		return;
	}

	times[0].tv_sec = time(NULL);
	times[0].tv_usec = times[1].tv_usec = 0;
	if (utimes(fname, times) < 0) {
		fprintf(stderr, "%s: ", fname);
		perror("utimes");
	}
}


/* actual file name returned in fname */
FILE *out_open(char *name, char *sfx, char *fname)
{
	int i;
	char *sp;
	FILE *rv;

	if (sout) {
		fname[0] = '\0';
		return stdout;
	}

	sprintf(fname, "%s.%s", name, sfx);

	/* ensure subdirectory exists */
	if (sp = strchr(fname, '/')) {
		*sp = 0;
                if (mkdir(fname, 0777) < 0 && errno != EEXIST) {
                        fprintf(stderr, "%s: mkdir: ", sp+1);
                        perror(fname);
                        return NULL;
                }
		*sp = '/';
	}

	for (i = 0; i < 100; i++) {
		rv = fopen(fname, "wx");
		if (rv) {
			printf("Extracting to %s\n", fname);
			break;
		}
		if (errno != EEXIST) {
			perror(fname);
			break;
		}
		sprintf(fname, "%s.%d.%s", name, i+1, sfx);
	}

	return rv;
}


void out_close(FILE *of)
{
	if (of == stdout)
		return;

	fclose(of);
}
