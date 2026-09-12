# libwildlifesystems - WildlifeSystems shared sensor utilities
# Static library for common sensor driver functionality

CC = gcc
AR = ar
CFLAGS = -Wall -Wextra -Werror -std=c99 -O2 -fPIC
ARFLAGS = rcs

# Version from debian/changelog
VERSION := $(shell dpkg-parsechangelog -S Version 2>/dev/null || echo "1.0.0")
CFLAGS += -DVERSION=\"$(VERSION)\"

# Directories
SRCDIR = src
BUILDDIR = build

# Library name
LIB = libwildlifesystems.a

# Tools built against the library. ws-emit lets shell drivers emit readings
# through the library rather than reimplementing the template in jq.
TOOLDIR = tools
TOOLS = $(BUILDDIR)/ws-emit

# Source and object files
SRC = $(SRCDIR)/ws_utils.c
OBJ = $(BUILDDIR)/ws_utils.o

# Install directories
DESTDIR ?=
PREFIX ?= /usr
LIBDIR = $(PREFIX)/lib
INCLUDEDIR = $(PREFIX)/include/ws
BINDIR = $(PREFIX)/bin

.PHONY: all clean install tools

all: $(BUILDDIR) $(LIB) $(TOOLS)

tools: $(BUILDDIR) $(TOOLS)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c $(SRCDIR)/ws_utils.h
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJ)
	$(AR) $(ARFLAGS) $@ $^

$(BUILDDIR)/ws-emit: $(TOOLDIR)/ws-emit.c $(LIB) $(SRCDIR)/ws_utils.h
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ $< $(LIB)

install: $(LIB) $(TOOLS)
	install -d $(DESTDIR)$(LIBDIR)
	install -m 644 $(LIB) $(DESTDIR)$(LIBDIR)/
	install -d $(DESTDIR)$(INCLUDEDIR)
	install -m 644 $(SRCDIR)/ws_utils.h $(DESTDIR)$(INCLUDEDIR)/
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(BUILDDIR)/ws-emit $(DESTDIR)$(BINDIR)/

clean:
	rm -rf $(BUILDDIR) $(LIB)
