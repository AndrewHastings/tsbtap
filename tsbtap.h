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

#define dprint(x)	if (debug) printf x

#define BE16(bp)	(((bp)[0] << 8) | (bp)[1])
#define MIN(a, b)	((a) < (b) ? (a) : (b))

#define SYSLVL_2000F	3500	/* 2000F option 210/215 */
#define FEATLVL_2000F	200
#define SYSLVL_ACCESS	5000	/* Access release A */
#define FEATLVL_ACCESS	1000

extern int is_access;
extern int ignore_errs;
extern int debug;
extern int verbose;

extern int is_tsb_label(unsigned char *tbuf, int nbytes);
extern void print_direntry(unsigned char *dbuf);
extern void print_number(SINK *snp, unsigned char *buf);
