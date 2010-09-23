/*
	bast - ZX Basic text to tape
	
	Copyright Edward Cree, 2010
	License: GNU GPL v3+
	
	tokens: ZX Basic tokens data
*/

#include <stdlib.h>

int ntokens;
typedef struct
{
	char *text;
	unsigned char tok; // multi-character tokens are in range 0xA3-0xFF.  0xA3 SPECTRUM and 0xA4 PLAY for 128k only
	// argument format / lexical rules are handled separately (if at all)
	char *data; // ancillary data; used when parsing (not used in token definitions)
}
token;

token * tokentable;

void mktoktbl(void);
void addtokd(char *text, unsigned char tok);
void addtoken(token data);
