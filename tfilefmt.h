/*
 * Copyright 2024 Andrew B. Hastings. All rights reserved.
 *
 */

/*
 * Routines for reading tapes containing files delimited by tapemarks.
 */

#ifndef _TFILEFMT_H
#define _TFILEFMT_H 1

#include "simtap.h"

typedef struct {
	TAPE	*tfile_tap;
	char	*tfile_buf;
	char	*tfile_bp;
	int	tfile_nleft;
	int	tfile_ateof;
} tfile_ctx_t;

extern void tfile_ctx_init(tfile_ctx_t *ctx, TAPE *tap, char *buf, int nbytes);
extern void tfile_ctx_fini(tfile_ctx_t *ctx);
extern int tfile_getbytes(tfile_ctx_t *ctx, char *buf, int nbytes);
extern int tfile_skipbytes(tfile_ctx_t *ctx, int nbytes);
extern int tfile_skipf(tfile_ctx_t *ctx);

#endif /* _TFILEFMT_H */
