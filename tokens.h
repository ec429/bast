/*
	bast - ZX Basic text to tape
	
	Copyright Edward Cree, 2010
	License: GNU GPL v3+
	
	tokens: ZX Basic tokens data
*/

#include <stdlib.h>

#define TOKEN_ZXFLOAT	0x0E
#define TOKEN_STRING	0x0F
#define TOKEN_VAR		0x11
#define TOKEN_VARSTR	0x12
#define TOKEN_NONPRINT	0x13
#define TOKEN_LABEL		0x14
#define TOKEN_PTRLBL	0x15
#define TOKEN_RLINK		0x18

int ntokens;
typedef struct
{
	char *text;
	unsigned char tok; // multi-character tokens are in range 0xA3-0xFF.  0xA3 SPECTRUM and 0xA4 PLAY for 128k only
	// argument format / lexical rules are handled separately (if at all)
	char *data; // ancillary data; used when parsing (not used in token definitions)
	int dl; // data length; used for eg. ~link (becomes a REM, 0xEA, with data possibly containing NULs).  If -1, data2 points to a struct bin_seg
	char *data2; // second ancillary data; used for eg. parsing ZXFLOATs
	signed char index; // 'offset from offset' in LABELs and PTRLBLs, eg. @foo+2A
}
token;

token * tokentable;

void mktoktbl(void);
void addtokd(char *text, unsigned char tok);
void addtoken(token data);
