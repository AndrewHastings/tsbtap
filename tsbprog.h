/*
 * Copyright 2024 Andrew B. Hastings. All rights reserved.
 *
 */

/*
 * TSB tokenized BASIC program routines.
 */

#ifndef _TSBPROG_H
#define _TSBPROG_H 1

extern char *dump_program(tfile_ctx_t *tfile, char *fn, unsigned char *dbuf);
extern char *extract_program(tfile_ctx_t *tfile, char *fn, char *oname,
			     unsigned char *dbuf);

#endif /* _TSBPROG_H */
