#
# Copyright 2024, 2025 Andrew B. Hastings. All rights reserved.
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

# If your build fails because of a missing "asan" library, try:
#   make clean
#   make CFLAGS=-Wno-trigraphs

CFLAGS=-g -fsanitize=address -Werror -Wno-trigraphs -Wunused-variable

HDRS = convert.h outfile.h simtap.h sink.h tfilefmt.h \
       tsbfile.h tsbprog.h tsbtap.h
OBJS = convert.o outfile.o simtap.o sink.o tfilefmt.o \
       tsbfile.o tsbprog.o tsbtap.o
LIBS = -lm

tsbtap: $(OBJS)
	$(CC) $(CFLAGS) -o tsbtap $^ $(LIBS)

clean:
	$(RM) $(OBJS)

clobber:
	$(RM) tsbtap $(OBJS)

%.o : %.c $(HDRS)
	$(CC) $(CFLAGS) -c $<
