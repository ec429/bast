/*
	bast - ZX Basic text to tape
	
	Copyright Edward Cree, 2010
	License: GNU GPL v3+
	
	x-tok: extract a token table from the (ROM+0x95) table
*/

#include <stdio.h>

int main(void)
{
	printf("SPECTRUM\t0xA3\n");
	printf("PLAY\t0xA4\n");
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
