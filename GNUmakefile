all:

include Makefrag.inc

VPATH=		.
all: $(PROG)
$(PROG): $(OBJS_BP)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS_BP) $(LIBS)
$(OBJS_BP): $(SRCS_FP) $(NONSRCS)
.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $<

REGRESS_FLAGS=	-f
regress:
	./test.sh $(REGRESS_FLAGS)
