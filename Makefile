OBJDIR := build
DLDIR := dl
CFLAGS := -Wall -g -O0 -fno-inline -Iinclude
CXXFLAGS := $(CFLAGS)

C_SRC := lib/daemonize.c lib/ini.c lib/mconf.c lib/misc.c lib/sock.c pktq.c
CXX_SRC := main.cpp proxy.cpp evq.cpp rfx_chat.cpp rfx_loot.cpp

VPATH = lib

all: proxy libutil.a $(DLDIR)/rfx_chat.so $(DLDIR)/rfx_loot.so $(DLDIR)/rfx_debug.so $(DLDIR)/rfx_inventory.so

proxy: $(OBJDIR)/main.o $(OBJDIR)/proxy.o $(OBJDIR)/evq.o \
       $(OBJDIR)/pktq.o api_version.c libutil.a
	$(CXX) -o $@ $^ -lev -ldl -lrt

$(DLDIR)/%.so: $(OBJDIR)/pic_%.o api_version.c librfxmod.a
	$(CXX) -shared -fPIC -DPIC -o $@ $^ -lrt

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

librfxmod.a: $(OBJDIR)/pic_pktq.o $(OBJDIR)/pic_evq.o $(OBJDIR)/pic_rfx_modules.o $(OBJDIR)/pic_misc.o
	ar cru $@ $^

.PHONY: dep

api_version.c: rfx_api.h pktq.h evq.h
	cat $^ | openssl sha1 | sed -e 's/.*= *//' -e 's/^/extern "C" const char rfx_api_ver[] = "/' -e 's/$$/";/' > $@

dep: api_version.c
	@$(CC) -MM $(CFLAGS) $(C_SRC) | sed -re 's/^([^:]+:)/$(OBJDIR)\/\1/' > .dep
	@$(CXX) -MM $(CFLAGS) $(CXX_SRC) | sed -re 's/^([^:]+:)/$(OBJDIR)\/\1/' >> .dep

clean:
	rm -f proxy libutil.a librfxmod.a build/*.o dl/*.so

tag:
	ctags -R .

-include Makefile.local
