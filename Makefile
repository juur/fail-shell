SHELL := /bin/sh

srcdir := .
objdir := obj

.SUFFIXES:
.SUFFIXES: .c .o

DESTDIR			:=
# /home/build/opt/host/bin/tcc
CC				:= gcc
CXX				:=
CFLAGS			:= -pedantic -Wall -Wextra -std=c99 -g -O
CPPFLAGS		:= -I$(srcdir) -I$(objdir)
LDFLAGS			:=
# /home/build/opt/lib64/libncurses.a
NCURSES_LD		:= 
CAT				:= cat
TAR				:= tar
YACC			:= byacc
Y_FLAGS			:= -d -t
L_FLAGS			:=
PKGCONFIG		:= pkg-config
INSTALL			:= install -c
INSTALL_PROGRAM	:= $(INSTALL)
INSTALL_DATA	:= $(INSTALL) -m 644
HELP2MAN		:= help2man
# tcc does not support dependencies
DEPS			:= 1
PACKAGE			:= fail-shell
VERSION			:= $(shell date "+%Y-%m-%d")
skip_SRCS		:= vi.c sh.c sh_old.c make.c expr.c awk.c

prefix		:= /usr/local
datarootdir := $(prefix)/share
datadir     := $(datarootdir)
exec_prefix := $(prefix)
bindir      := $(exec_prefix)/bin
sbindir     := $(exec_prefix)/sbin
libexecdir  := $(exec_prefix)/libexec
docdir      := $(datarootdir)/doc
infodir     := $(datarootdir)/info
libdir      := $(prefix)/lib
mandir      := $(datarootdir)/man
localedir   := $(datarootdir)/locale

all_SRCS		:= $(filter-out $(skip_SRCS), $(notdir $(wildcard $(srcdir)/src/*.c)))
all_HEADERS		:= $(notdir $(wildcard $(srcdir)/src/*.h))
all_PACKAGES	:= $(addprefix $(objdir)/bin/,$(all_SRCS:.c=))
package_OBJS	:= $(addprefix $(objdir)/,$(filter-out sh.o,$(all_SRCS:.c=.o)))

ifeq ($(DEPS),1)
CPPFLAGS += -MMD -MP
endif
CPPFLAGS += -I$(srcdir)/src


.PHONY: all
# $(objdir)/bin/awk
all: $(objdir)/.d $(all_PACKAGES) $(objdir)/bin/sh $(objdir)/bin/vi $(objdir)/bin/make

$(objdir)/.d:
	@mkdir -p $(objdir)/.d 2>/dev/null

$(all_PACKAGES): $(objdir)/bin/%: $(objdir)/%.o
	$(CC) $(LDFLAGS) $< -o $@


$(objdir)/bin/sh: $(objdir)/sh.o $(objdir)/sh.y.tab.o
	$(CC) $(LDFLAGS) $^ -o $@

$(objdir)/bin/vi: $(objdir)/vi.o
	$(CC) $(LDFLAGS) $< -lncurses -o $@

$(objdir)/bin/awk: $(objdir)/awk.o $(objdir)/awk.y.tab.o $(objdir)/awk.grammar.yy.o
	$(CC) $(LDFLAGS) $^ -o $@

$(objdir)/bin/make: $(objdir)/make.o $(objdir)/make.y.tab.o $(objdir)/make.grammar.yy.o
	$(CC) $(LDFLAGS) $^ -o $@


$(objdir)/sh.o:	$(objdir)/sh.y.tab.h $(objdir)/sh.y.tab.c $(srcdir)/src/sh.c $(srcdir)/src/sh.h

$(objdir)/awk.o: $(objdir)/awk.y.tab.h $(objdir)/awk.y.tab.c $(srcdir)/src/awk.c

$(objdir)/make.o: $(objdir)/make.y.tab.h $(objdir)/make.y.tab.c $(srcdir)/src/make.c


$(objdir)/sh.y.tab.h $(objdir)/sh.y.tab.c:	$(srcdir)/src/sh.y $(srcdir)/src/sh.h
	$(YACC) $(Y_FLAGS) -b sh.y -o $@ $<


$(objdir)/make.grammar.yy.o: $(objdir)/make.grammar.yy.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

$(objdir)/make.grammar.yy.c: $(srcdir)/src/make.l $(srcdir)/src/make.h
	$(LEX) $(L_FLAGS) -o $@ $<

$(objdir)/make.y.tab.h $(objdir)/make.y.tab.c:	$(srcdir)/src/make.y $(srcdir)/src/make.h
	$(YACC) $(Y_FLAGS) -b make.y -o $@ $<


$(objdir)/awk.grammar.yy.o: $(objdir)/awk.grammar.yy.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

$(objdir)/awk.grammar.yy.c: $(srcdir)/src/awk.l $(srcdir)/src/awk.h
	$(LEX) $(L_FLAGS) -o $@ $<

$(objdir)/awk.y.tab.h $(objdir)/awk.y.tab.c:	$(srcdir)/src/awk.y $(srcdir)/src/awk.h
	$(YACC) $(Y_FLAGS) -b awk.y -o $@ $<


.PHONY: install uninstall

install: $(all_PACKAGES)

uninstall:


.PHONY: mostly-clean clean distclean maintainer-clean

mostlyclean:
	$(RM) $(package_OBJS) $(objdir)/*.yy.o $(objdir)/*.tab.o $(addprefix $(objdir)/,$(skip_SRCS:.c=.o))

clean: mostlyclean
	$(RM) $(all_PACKAGES) $(objdir)/bin/{make,sh,vi} $(objdir)/*.yy.c $(objdir)/*.tab.c $(objdir)/*.tab.h $(objdir)/.d/*.d

distclean: clean
	$(RM) config.log
	$(RM) $(objdir)/config.h{,~}
	$(RM) -r $(objdir)/.d

maintainer-clean: distclean
	$(RM) $(PACKAGE)-$(VERSION).tar.xz


.PHONY: dist
dist:
	pushd $(srcdir) >/dev/null ; \
	$(TAR) -acf $(objdir)/$(PACKAGE)-$(VERSION).tar.xz \
		--transform="s,^./,,;s,^,$(PACKAGE)-$(VERSION)/," \
		src Makefile ; \
	popd >/dev/null



$(objdir)/%.o: $(srcdir)/src/%.c
ifeq ($(DEPS),1)
	$(CC) -c $(CFLAGS) $(CPPFLAGS) -MF $(objdir)/.d/$*.d $< -o $@
else
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@
endif

ifeq ($(DEPS),1)
-include $(all_SRCS:$(srcdir)/src/%.c=$(objdir)/.d/%.d)
endif
