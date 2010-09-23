/*
	bast - ZX Basic text to tape
	
	Copyright Edward Cree, 2010
	License: GNU GPL v3+
	
	tokens: ZX Basic tokens data
*/

#include <stdlib.h>

#define TOKEN_VAR		0x01
#define TOKEN_VARSTR	0x02
#define TOKEN_NONPRINT	0x03
#define TOKEN_ZXFLOAT	0x0E
#define TOKEN_STRING	0x0F
#define TOKEN_RLINK		0x18
#define TOKEN_LABEL		0x04
#define TOKEN_PTRLBL	0x05

int ntokens;
typedef struct
{
	char *text;
	unsigned char tok; // multi-character tokens are in range 0xA3-0xFF.  0xA3 SPECTRUM and 0xA4 PLAY for 128k only
	// argument format / lexical rules are handled separately (if at all)
	char *data; // ancillary data; used when parsing (not used in token definitions)
	int dl; // data length; used for eg. ~link (becomes a REM, 0xEA, with data possibly containing NULs).  If -1, data2 points to a struct bin_seg
	char *data2; // second ancillary data; used for eg. parsing ZXFLOATs
}
token;

token * tokentable;

void mktoktbl(void);
void addtokd(char *text, unsigned char tok);
void addtoken(token data);
