OBJDIR := build
DLDIR := dl
CFLAGS := -Wall -g -O0 -fno-inline -Iinclude
CXXFLAGS := $(CFLAGS)

C_SRC := lib/daemonize.c lib/ini.c lib/mconf.c lib/misc.c lib/sock.c pktq.c
CXX_SRC := main.cpp proxy.cpp evq.cpp rfx_chat.cpp

VPATH = lib

all: proxy libutil.a $(DLDIR)/rfx_chat.so

proxy: $(OBJDIR)/main.o $(OBJDIR)/proxy.o $(OBJDIR)/evq.o \
       $(OBJDIR)/pktq.o $(OBJDIR)/api_version.o libutil.a
	$(CXX) -o $@ $^ -lev -ldl

$(DLDIR)/%.so: $(OBJDIR)/pic_%.o $(OBJDIR)/pic_api_version.o
	$(CXX) -shared -o $@ $^

ifeq ($(filter dep clean,$(MAKECMDGOALS)),)
.dep: dep
-include .dep
endif

$(OBJDIR)/%.o: %.c
	$(CC) -c -o $@ $(CFLAGS) $<

$(OBJDIR)/%.o: %.cpp
	$(CXX) -c -o $@ $(CXXFLAGS) $<

$(OBJDIR)/pic_%.o: %.c
	$(CC) -c -fpic -DPIC -o $@ $(CFLAGS) $<

$(OBJDIR)/pic_%.o: %.cpp
	$(CXX) -c -fpic -DPIC -o $@ $(CXXFLAGS) $<

libutil.a: $(OBJDIR)/daemonize.o $(OBJDIR)/mconf.o $(OBJDIR)/misc.o \
		$(OBJDIR)/sock.o
	ar cru $@ $^

.PHONY: dep

api_version.c: rfx_api.h pktq.h evq.h
	cat $^ | openssl sha1 | sed -e 's/[^ =]*[ =]*//' -e 's/^/const char rfx_api_ver[] = "/' -e 's/$$/";/' > $@

dep: api_version.c
	@$(CC) -MM $(CFLAGS) $(C_SRC) | sed -re 's/^([^:]+:)/$(OBJDIR)\/\1/' > .dep
	@$(CXX) -MM $(CFLAGS) $(CXX_SRC) | sed -re 's/^([^:]+:)/$(OBJDIR)\/\1/' >> .dep

clean:
	rm -f proxy libutil.a build/*.o

tag:
	ctags -R .

-include Makefile.local
