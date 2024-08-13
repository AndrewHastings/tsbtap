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
 * TSB file routines.
 */

#ifndef _TSBFILE_H
#define _TSBFILE_H 1

extern char *extract_ascii_file(tfile_ctx_t *tfile, char *fn, char *oname,
				unsigned char *dbuf);
extern char *extract_basic_file(tfile_ctx_t *tfile, char *fn, char *oname,
				unsigned char *dbuf);

#endif /* _TSBFILE_H */
