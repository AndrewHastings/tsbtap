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
 * Routines for reading/writing SIMH-format tape images.
 */

#ifndef _SIMTAP_H
#define _SIMTAP_H 1

typedef struct {
	FILE		*tp_fp;
	char		*tp_path;
	char		*tp_buf;
	uint32_t	tp_nbytes;
	uint8_t		tp_status;
} TAPE;

extern TAPE *tap_open(char *path);
extern void tap_close(TAPE *tap);
extern ssize_t tap_readblock(TAPE *tap, char **bufp);

#endif /* _SIMTAP_H */
