/*
 * Copyright 2024 Andrew B. Hastings. All rights reserved.
 *
 */

/*
 * Read HP2000 TSB dump tapes in SIMH tape image format.
 */

#define dprint(x)	if (debug) printf x

#define BE16(bp)	(((bp)[0] << 8) | (bp)[1])
#define MIN(a, b)	((a) < (b) ? (a) : (b))

extern int is_access;
extern int debug;
extern int verbose;

extern void print_direntry(unsigned char *dbuf);
extern void print_number(FILE *fp, unsigned char *buf);
