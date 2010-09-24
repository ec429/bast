# Makefile for bast, ZX Basic compiler

PREFIX ?= /usr/local
CC ?= gcc
CFLAGS ?= -Wall
AWK ?= awk

all: bast test.tap

install: bast
	install -D bast $(PREFIX)/bin/bast

bast: bast.c tokens.o tokens.h version.h
	$(CC) $(CFLAGS) -o bast bast.c tokens.o -lm

version.h: version
	./gitversion

version:
	touch version

tokens.o: tokens.c tokens.h addtokens.c

tokens: toktbl x-tok
	./x-tok < toktbl > tokens

x-tok: x-tok.c
	$(CC) $(CFLAGS) -o x-tok x-tok.c

addtokens.c: tokens mkaddtokens.awk
	$(AWK) -f mkaddtokens.awk < tokens > addtokens.c

%.o: %.c %.h
	$(CC) $(CFLAGS) -o $@ -c $<

test.tap: bast test.bas
	./bast -b test.bas -l test.obj -t test.tap -W all -O cut-numbers
