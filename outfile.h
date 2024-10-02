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
 * Output file utility routines.
 */

#ifndef _OUTFILE_H
#define _OUTFILE_H 1

extern int sout;

extern SINK *out_open(char *name, char *sfx, char *fname);
extern void out_close(SINK *snp);
extern char *name_match(char *pattern, char *id, char *name);
extern int jdate_to_tm(int yr, int jday, struct tm *tm);
extern void set_mtime(char *fname, struct tm *tm);

#endif /* _OUTFILE_H */
