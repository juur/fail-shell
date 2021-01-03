SHELL := /bin/sh

srcdir := .
objdir := obj

.SUFFIXES:
.SUFFIXES: .c .o

DESTDIR			:=
# /home/build/opt/host/bin/tcc
CC				:= gcc
CXX				:=
#CFLAGS			:= -pedantic -Wall -Wextra -std=c99 -g -O -Wno-unused-function -Wno-unused-parameter
CFLAGS			:= -std=c99 -g -O -Wno-unused-function -Wno-unused-parameter -Wall
CPPFLAGS		:= -I$(srcdir) -I$(objdir) -D_XOPEN_SOURCE=700
LDFLAGS			:= -lncurses
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
skip_SRCS		:= vi.c sh.c sh_old.c make.c expr.c
broken_SRCS		:= awk.c

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
all_SPECIAL_PACKAGES := $(addprefix $(objdir)/bin/,$(skip_SRCS:.c=))
package_OBJS		 := $(addprefix $(objdir)/,$(all_SRCS:.c=.o))
skip_OBJS			 := $(addprefix $(objdir)/,$(skip_OBJS:.c=.o))

ifeq ($(DEPS),1)
CPPFLAGS += -MMD -MP
endif
CPPFLAGS += -I$(srcdir)/src


.PHONY: all

all: $(objdir)/.d $(all_PACKAGES) $(all_SPECIAL_PACKAGES)


$(objdir)/.d:
	@mkdir -p $(objdir)/.d 2>/dev/null

$(all_PACKAGES): $(objdir)/bin/%: $(objdir)/%.o
	$(CC) $(LDFLAGS) $< -o $@



$(objdir)/bin/vi: $(objdir)/vi.o
	$(CC) $(LDFLAGS) $< -lncurses -o $@

$(objdir)/bin/awk: $(objdir)/awk.y.tab.o $(objdir)/awk.grammar.yy.o $(objdir)/awk.o  
	$(CC) $(LDFLAGS) $^ -o $@

$(objdir)/bin/make: $(objdir)/make.y.tab.o $(objdir)/make.grammar.yy.o $(objdir)/make.o 
	$(CC) $(LDFLAGS) $^ -o $@

$(objdir)/bin/sh: $(objdir)/sh.y.tab.o $(objdir)/sh.grammar.yy.o $(objdir)/sh.o
	$(CC) $(LDFLAGS) $^ -o $@

$(objdir)/bin/expr: $(objdir)/expr.y.tab.o $(objdir)/expr.o
	$(CC) $(LDFLAGS) $^ -o $@



$(objdir)/%.grammar.yy.o: $(objdir)/%.grammar.yy.c $(objdir)/%.grammar.yy.h

$(objdir)/%.grammar.yy.h $(objdir)/%.grammar.yy.c: $(srcdir)/src/%.l $(srcdir)/src/%.h
	$(LEX) $(L_FLAGS) --header-file=$(objdir)/$(<F:%.l=%.grammar.yy.h) -o $@ $<


$(objdir)/%.y.tab.o: $(objdir)/%.y.tab.c $(objdir)/%.tab.h

$(objdir)/%.y.tab.c $(objdir)/%.y.tab.h: $(srcdir)/src/%.y $(srcdir)/src/%.h
	$(YACC) $(Y_FLAGS) -b $*.y -o $@ $<


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
		*.y.tab.h

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
