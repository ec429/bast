/*
	bast - ZX Basic text to tape
	
	Copyright Edward Cree, 2010
	License: GNU GPL v3+
	
	tokens: ZX Basic tokens data
*/

#include "tokens.h"

void mktoktbl(void)
{
	ntokens=0;
	tokentable=NULL;
	#include "addtokens.c"
	addtokd("GOTO", 0xEC);  // alias GOTO -> GO TO
	addtokd("GOSUB", 0xED); // alias GOSUB -> GO SUB
	addtokd("+", '+');
	addtokd("-", '-');
	addtokd("*", '*');
	addtokd("/", '/');
	addtokd("^", '^');
	addtokd("=", '=');
	addtokd(">", '>');
	addtokd("<", '<');
	addtokd("(", '(');
	addtokd(")", ')');
	addtokd(",", ',');
	addtokd(";", ';');
	addtokd(":", ':');
	addtokd("#", '#');
}

void addtokd(char *text, unsigned char tok)
{
	token data;
	data.text=text;
	data.tok=tok;
	addtoken(data);
}

void addtoken(token data)
{
	ntokens++;
	tokentable=(token *)realloc(tokentable, ntokens*sizeof(token));
	tokentable[ntokens-1]=data;
}
