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
 * Routines for converting tapes between 2000F and 2000 Access.
 */

#ifndef _CONVERT_H
#define _CONVERT_H 1

#define STLEN_ACCESS	999	/* TBD */
#define STLEN_2000F	204	/* TBD */

extern int do_aopt(TAPE *tap, TAPE *ot);
extern int do_copt(TAPE *tap, TAPE *ot);

#endif /* _CONVERT_H */
