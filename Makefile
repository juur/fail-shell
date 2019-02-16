SHELL := /bin/sh

srcdir := .
objdir := /home/build/opt

.SUFFIXES:
.SUFFIXES: .c .o

DESTDIR			:=
CC				:= /home/build/opt/host/bin/tcc
CXX				:=
CFLAGS			:= -O2 -Wpedantic -Wall -Wextra -std=c99
CPPFLAGS		:=
LDFLAGS			:= -static
CAT				:= cat
TAR				:= tar
PKGCONFIG		:= pkg-config
INSTALL			:= install -c
INSTALL_PROGRAM	:= $(INSTALL)
INSTALL_DATA	:= $(INSTALL) -m 644
HELP2MAN		:= help2man
DEPS			:= 0
PACKAGE			:= zero-shell
VERSION			:= $(shell date "+%Y-%m-%d")

prefix		:= /usr/local
datarootdir := $(prefix)/share
datadir     := $(datarootdir)
exec_prefix := $(prefix)
bindir      := $(exec_prefix)/bin
sbindir     := $(exec_prefix)/sbin
libexecdir  := $(exec_prefix)/libexec
docdir      := @@DOCDIR@@
infodir     := $(datarootdir)/info
libdir      := $(prefix)/lib
mandir      := $(datarootdir)/man
localedir   := $(datarootdir)/locale

all_SRCS		:= $(wildcard $(srcdir)/src/*.c)
all_HEADERS		:= $(wildcard $(srcdir)/src/*.h)
all_PACKAGES	:= $(addprefix $(objdir)/,$(notdir $(all_SRCS:.c=)))
package_OBJS	:= $(addprefix $(objdir)/,$(notdir $(all_SRCS:.c=.o)))

ifeq ($(DEPS),1)
CPPFLAGS += -MMD -MP
endif
CPPFLAGS += -I$(srcdir)/src


.PHONY: all
all: $(all_PACKAGES) $(objdir)/.d

$(objdir)/.d:
	@mkdir -p $(objdir)/.d 2>/dev/null

$(all_PACKAGES): $(objdir)/%: $(objdir)/%.o
	$(CC) $(LDFLAGS) $< -o $@


.PHONY: install uninstall

install: $(all_PACKAGES)

uninstall:


.PHONY: mostly-clean clean distclean maintainer-clean

mostlyclean:
	$(RM) $(all_PACKAGES) $(package_OBJS)

clean: mostlyclean

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
