/*
 * Copyright 2024 Andrew B. Hastings. All rights reserved.
 *
 */

/*
 * TSB file routines.
 */

#ifndef _TSBFILE_H
#define _TSBFILE_H 1

extern char *extract_ascii_file(tfile_ctx_t *tfile, char *fn, char *oname,
				unsigned char *dbuf);
extern char *extract_basic_file(tfile_ctx_t *tfile, char *fn, char *oname,
				unsigned char *dbuf);

#endif /* _TSBFILE_H */
