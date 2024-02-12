#
# Copyright 2024 Andrew B. Hastings. All rights reserved.
#

OBJS = tsbtap.o

cdctap: $(OBJS)
	$(CC) $(CFLAGS) -o tsbtap $^

clean:
	$(RM) $(OBJS)

clobber:
	$(RM) tsbtap $(OBJS)
