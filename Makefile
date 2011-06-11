# Makefile for bast, ZX Basic compiler

PREFIX ?= /usr/local
CC ?= gcc
CFLAGS ?= -Wall -Wextra -pedantic --std=gnu99 -Werror -g
AWK ?= awk
VERSION:="`./mkversion`"

all: bast objify test.tap

install: bast objify
	install -D bast $(PREFIX)/bin/bast
	install -D objify $(PREFIX)/bin/objify

bast: bast.c tokens.o tokens.h version.h
	$(CC) $(CFLAGS) -o bast bast.c tokens.o -lm

mkversion: mkversion.c version.h
	$(CC) $(CFLAGS) -o mkversion mkversion.c

version.h: version
	./gitversion

version:
	touch version

tokens.o: tokens.c tokens.h addtokens.c

tokens: toktbl x-tok
	./x-tok < toktbl > tokens

addtokens.c: tokens mkaddtokens.awk
	$(AWK) -f mkaddtokens.awk < tokens > addtokens.c

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

%.o: %.c %.h
	$(CC) $(CFLAGS) -o $@ -c $<

test.tap: bast test.bas test.obj
	./bast -b test.bas -l test.obj -t test.tap -W all -O cut-numbers

dist: all mkversion
	-mkdir bast_$(VERSION)
	for p in *; do cp $$p bast_$(VERSION)/$$p; done;
	-rm bast_$(VERSION)/*.tap
	-rm bast_$(VERSION)/*.tar.gz
	tar -cvvf bast_$(VERSION).tar bast_$(VERSION)
	gzip bast_$(VERSION).tar
	-rm -r bast_$(VERSION)

