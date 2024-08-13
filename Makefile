#
# Copyright 2024 Andrew B. Hastings. All rights reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#

CFLAGS=-g -fsanitize=address -Wno-trigraphs -Wunused-variable

HDRS = outfile.h simtap.h tfilefmt.h tsbfile.h tsbprog.h tsbtap.h
OBJS = outfile.o simtap.o tfilefmt.o tsbfile.o tsbprog.o tsbtap.o
LIBS = -lm

tsbtap: $(OBJS)
	$(CC) $(CFLAGS) -o tsbtap $^ $(LIBS)

clean:
	$(RM) $(OBJS)

clobber:
	$(RM) tsbtap $(OBJS)

%.o : %.c $(HDRS)
	$(CC) $(CFLAGS) -c $<
