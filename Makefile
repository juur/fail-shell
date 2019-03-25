SHELL := /bin/sh

srcdir := .
objdir := /home/build/opt

.SUFFIXES:
.SUFFIXES: .c .o

DESTDIR			:=
CC				:= /home/build/opt/host/bin/tcc
CXX				:=
CFLAGS			:= -Wpedantic -Wall -Wextra -std=c99 -g -O
CPPFLAGS		:= -I$(srcdir) -I$(objdir)
LDFLAGS			:= -static -g
NCURSES_LD		:= /home/build/opt/lib64/libncurses.a
CAT				:= cat
TAR				:= tar
YACC			:= byacc
Y_FLAGS			:= -d -t
PKGCONFIG		:= pkg-config
INSTALL			:= install -c
INSTALL_PROGRAM	:= $(INSTALL)
INSTALL_DATA	:= $(INSTALL) -m 644
HELP2MAN		:= help2man
# tcc does not support dependencies
DEPS			:= 0
PACKAGE			:= zero-shell
VERSION			:= $(shell date "+%Y-%m-%d")
skip_SRCS		:= vi.c sh.c sh_old.c

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
all: $(all_PACKAGES) $(objdir)/.d $(objdir)/bin/sh

$(objdir)/.d:
	@mkdir -p $(objdir)/.d 2>/dev/null

$(all_PACKAGES): $(objdir)/bin/%: $(objdir)/%.o
	$(CC) $(LDFLAGS) $< -o $@

$(objdir)/bin/sh: $(objdir)/sh.o $(objdir)/y.tab.o
	$(CC) $(LDFLAGS) $^ -o $@

$(objdir)/sh.o:	$(objdir)/y.tab.h $(objdir)/y.tab.c

$(objdir)/y.tab.h $(objdir)/y.tab.c:	$(srcdir)/src/grammar.y $(srcdir)/src/sh.h
	$(YACC) $(Y_FLAGS) $<

.PHONY: install uninstall

install: $(all_PACKAGES)

uninstall:


.PHONY: mostly-clean clean distclean maintainer-clean

mostlyclean:
	$(RM) $(package_OBJS) y.tab.o $(addprefix $(objdir)/,$(skip_SRCS:.c=.o))

clean: mostlyclean
	$(RM) $(all_PACKAGES) y.tab.c y.tab.h

distclean: clean
	$(RM) config.log
	$(RM) $(objdir)/config.h{,~}
	$(RM) $(objdir)/.d

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
