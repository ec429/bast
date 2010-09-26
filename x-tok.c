/*
	bast - ZX Basic text to tape
	
	Copyright Edward Cree, 2010
	License: GNU GPL v3+
	
	x-tok: extract a token table from the (ROM+0x95) table
*/

#include <stdio.h>

int main(void)
{
	// SE BASIC: DELETE, EDIT, RENUM, PALETTE, SOUND, and ON ERR - 0x00 to 0x05
	printf("DELETE\t0x00\n");
	printf("EDIT\t0x01\n");
	printf("RENUM\t0x02\n");
	printf("PALETTE\t0x03\n");
	printf("SOUND\t0x04\n");
	printf("ON ERR\t0x05\n");
	// +3 BASIC: SPECTRUM and PLAY - 0xA3 to 0xA4
	printf("SPECTRUM\t0xA3\n");
	printf("PLAY\t0xA4\n");
	// 48 BASIC: extract from the ROM - 0xA5 to 0xFF
	getchar();
	unsigned char tok=0xA5;
	char curr[80];
	int i=0;
	int c;
	while(!feof(stdin))
	{
		c=getchar();
		if(c==EOF)
			break;
		curr[i++]=c&0x7F;curr[i]=0;
		while(!(c&0x80))
		{
			c=getchar();
			if(c==EOF)
				break;
			curr[i++]=c&0x7F;curr[i]=0;
		}
		printf("%s\t0x%02X\n", curr, tok++);
		i=0;
	}
	return(0);
}
