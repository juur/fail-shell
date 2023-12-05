SHELL := /bin/sh

srcdir := .
objdir := obj

.SUFFIXES:
.SUFFIXES: .c .o

DESTDIR	:=
CC		:= gcc
CXX		:=
CFLAGS	:= \
	-std=c11 \
	-ggdb3 \
	-fno-builtin \
	-Wno-unused-function \
	-Wno-unused-parameter \
	-Wno-unused-label \
	-Wall -Wextra \
	-Wformat=2 \
	-fdiagnostics-color \
	-O3

NDEBUG			:=
CPPFLAGS		:= -I$(srcdir) -I$(objdir) $(NDEBUG)
LDFLAGS			:= 
ifeq ($(FAIL),1)
NCURSES_LD		:= 
else
NCURSES_LD		:= -lncurses
endif
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
skip_SRCS		:= vi.c sh.c sh_old.c make.c expr.c
broken_SRCS		:= awk.c sh_old.c
extra_PACKAGES  := chown

# fail libc support pass FAIL=1 to make
ifeq ($(FAIL),1)
FAIL_INC		:= ../fail-libc/include
FAIL_LIB		:= ../fail-libc/lib
# these don't work with fail-libc yet, at all
#broken_SRCS     += mount.c make.c vi.c sh.c
#broken_SRCS		+= vi.c
CFLAGS			+= -nostdinc -I$(FAIL_INC)
LDFLAGS			+= -nostdlib -L$(FAIL_LIB) -lc $(FAIL_LIB)/crt1.o
endif

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

all_SRCS			 := $(filter-out $(skip_SRCS), $(notdir $(wildcard $(srcdir)/src/*.c)))
all_SRCS			 := $(filter-out $(broken_SRCS), $(all_SRCS))
all_HEADERS			 := $(notdir $(wildcard $(srcdir)/src/*.h))
all_PACKAGES		 := $(addprefix $(objdir)/bin/,$(all_SRCS:.c=))
all_EXTRA_PACKAGES   := $(addprefix $(objdir)/bin/,$(extra_PACKAGES))
all_SPECIAL_PACKAGES := $(addprefix $(objdir)/bin/,$(filter-out $(broken_SRCS:.c=), $(skip_SRCS:.c=)))
package_OBJS		 := $(addprefix $(objdir)/,$(all_SRCS:.c=.o))
skip_OBJS			 := $(addprefix $(objdir)/,$(skip_OBJS:.c=.o))

ifeq ($(DEPS),1)
CPPFLAGS += -MMD -MP
endif

CPPFLAGS += -I$(srcdir)/src


.PHONY: all

all: $(objdir)/.d $(all_PACKAGES) $(all_SPECIAL_PACKAGES) $(all_EXTRA_PACKAGES)


$(objdir)/.d:
	@mkdir -p $(objdir)/.d 2>/dev/null

$(all_PACKAGES): $(objdir)/bin/%: $(objdir)/%.o
	$(CC) $< $(LDFLAGS) -o $@

$(objdir)/bin/chown: $(objdir)/chgrp.o
	$(CC) $< $(LDFLAGS) -o $@

$(objdir)/bin/vi: $(objdir)/vi.o
	$(CC) $< $(LDFLAGS) $(NCURSES_LD) -o $@

$(objdir)/bin/awk: $(objdir)/awk.y.tab.o $(objdir)/awk.grammar.yy.o $(objdir)/awk.o  
	$(CC) $^ $(LDFLAGS) -o $@

$(objdir)/bin/make: $(objdir)/make.y.tab.o $(objdir)/make.grammar.yy.o $(objdir)/make.o 
	$(CC) $^ $(LDFLAGS) -o $@

$(objdir)/bin/sh: $(objdir)/sh.y.tab.o $(objdir)/sh.grammar.yy.o $(objdir)/sh.o
	$(CC) $^ $(LDFLAGS) -o $@

$(objdir)/bin/expr: $(objdir)/expr.y.tab.o $(objdir)/expr.o
	$(CC) $^ $(LDFLAGS) -o $@



$(objdir)/%.grammar.yy.o: $(objdir)/%.grammar.yy.c $(objdir)/%.grammar.yy.h
	$(CC) $(subst,-fanalyzer,,$(CFLAGS)) $(CPPFLAGS) -D_XOPEN_SOURCE=700 -c -o $@ $<

$(objdir)/%.grammar.yy.c $(objdir)/%.grammar.yy.h: $(srcdir)/src/%.l $(srcdir)/src/%.h
	$(LEX) $(L_FLAGS) --header-file=$(objdir)/$(<F:%.l=%.grammar.yy.h) -o $(objdir)/$(<F:%.l=%.grammar.yy.c) $<


$(objdir)/%.y.tab.o: $(objdir)/%.y.tab.c $(objdir)/%.tab.h
	$(CC) $(subst,-fanalyzer,,$(CFLAGS)) $(CPPFLAGS) -D_XOPEN_SOURCE=700 -c -o $@ $<

$(objdir)/%.y.tab.c $(objdir)/%.y.tab.h: $(srcdir)/src/%.y $(srcdir)/src/%.h
	$(YACC) $(Y_FLAGS) -b $*.y -o $(objdir)/$(<F:%.y=%.y.tab.c) $<


.PHONY: install uninstall

install: $(all_PACKAGES)

uninstall:


.PHONY: mostly-clean clean distclean maintainer-clean

mostlyclean:
	$(RM) $(package_OBJS) $(skip_OBJS) $(objdir)/*.yy.o $(objdir)/*.tab.o

clean: mostlyclean
	$(RM) $(all_PACKAGES) $(objdir)/bin/{make,sh,vi} \
		$(objdir)/*.yy.c $(objdir)/*.tab.c \
		$(objdir)/*.tab.h $(objdir)/.d/*.d \
		$(objdir)/*.grammar.yy.[cdh] \
		$(objdir)/*.tab.[cdh] \
		$(objdir)/*.o \
		*.y.tab.h \
		*.grammar.yy.h

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
-include $(skip_SRCS:$(srcdir)/src/%.c=$(objdir)/.d/%.d)
endif
