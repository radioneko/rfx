OBJDIR := build
CFLAGS := -Wall -g -O0 -fno-inline -Iinclude
CXXFLAGS := $(CFLAGS)

C_SRC := lib/daemonize.c lib/ini.c lib/mconf.c lib/misc.c lib/sock.c pktq.c
CXX_SRC := main.cpp

VPATH = lib

all: proxy libutil.a

proxy: $(OBJDIR)/main.o libutil.a
	$(CXX) -o $@ $^ -lev

ifeq ($(filter dep clean,$(MAKECMDGOALS)),)
.dep: dep
-include .dep
endif

$(OBJDIR)/%.o: %.c
	$(CC) -c -o $@ $(CFLAGS) $<

$(OBJDIR)/%.o: %.cpp
	$(CXX) -c -o $@ $(CXXFLAGS) $<

libutil.a: $(OBJDIR)/daemonize.o $(OBJDIR)/mconf.o $(OBJDIR)/misc.o \
		$(OBJDIR)/sock.o $(OBJDIR)/pktq.o
	ar cru $@ $^

.PHONY: dep

dep:
	@$(CC) -MM $(CFLAGS) $(C_SRC) | sed -re 's/^([^:]+:)/$(OBJDIR)\/\1/' > .dep
	@$(CXX) -MM $(CFLAGS) $(CXX_SRC) | sed -re 's/^([^:]+:)/$(OBJDIR)\/\1/' >> .dep


STREAM := c2s
STREAM := s2c

listen:
	-nc -l -p 27780 > /tmp/$(STREAM) &

play:
	nc 127.0.0.1 1234 -q1 < 1/$(STREAM)

r: proxy listen
	strace -e epoll_wait,epoll_ctl,readv,writev,connect,accept,close ./$<
#	./$<

v: proxy listen
	valgrind --tool=memcheck --leak-check=full --show-reachable=yes -v ./$<

g: proxy listen
	gdb --args ./$<

t:
	cmp /tmp/$(STREAM) 1/$(STREAM)
