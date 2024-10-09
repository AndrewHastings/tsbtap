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
 * TSB tokenized BASIC program routines.
 */

#ifndef _TSBPROG_H
#define _TSBPROG_H 1

typedef struct {
	unsigned char	*pg_buf;
	unsigned char	*pg_bp;		/* sequential read position */
	int		pg_sz;		/* program text w/out symtab */
	int		pg_nread;	/* total read from tape */
} prog_ctx_t;

extern int prog_init(prog_ctx_t *prog, tfile_ctx_t *tfile);
extern void prog_setsz(prog_ctx_t *prog, int nbytes);
extern int prog_nleft(prog_ctx_t *prog);
extern int prog_getbytes(prog_ctx_t *prog, unsigned char **bufp, int nbytes);
extern int prog_getbytesat(prog_ctx_t *prog, unsigned char **bufp, int nbytes,
			   int off);
extern void prog_fini(prog_ctx_t *prog);

typedef struct {
	prog_ctx_t	*st_ctx;
	int		st_nleft;	/* in bytes */
} stmt_ctx_t;

extern int stmt_init(stmt_ctx_t *ctx, prog_ctx_t *prog);
extern int stmt_getbytes(stmt_ctx_t *ctx, unsigned char **buf, int nbytes);
extern void stmt_fini(stmt_ctx_t *ctx);

extern char *print_stmt(SINK *snp, stmt_ctx_t *ctx);
extern char *un_csave(prog_ctx_t *prog, unsigned char *dbuf);
extern char *dump_program(tfile_ctx_t *tfile, char *fn, unsigned char *dbuf);
extern char *extract_program(tfile_ctx_t *tfile, char *fn, char *oname,
			     unsigned char *dbuf);

#endif /* _TSBPROG_H */
