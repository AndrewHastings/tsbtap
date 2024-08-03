/*
 * Copyright 2024 Andrew B. Hastings. All rights reserved.
 *
 */

/*
 * Output file utility routines.
 */

#ifndef _OUTFILE_H
#define _OUTFILE_H 1

extern int sout;

extern FILE *out_open(char *name, char *sfx, char *fname);
extern void out_close(FILE *of);
extern char *name_match(char *pattern, char *id, char *name);
extern int jdate_to_tm(int yr, int jday, struct tm *tm);
extern void set_mtime(char *fname, struct tm *tm);

#endif /* _OUTFILE_H */
