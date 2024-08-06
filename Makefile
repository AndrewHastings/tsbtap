#
# Copyright 2024 Andrew B. Hastings. All rights reserved.
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
