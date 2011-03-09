/*
	bast - ZX Basic text to tape
	
	Copyright Edward Cree, 2010
	License: GNU GPL v3+
	
	objify: convert flat binary to .obj format
*/

#define _GNU_SOURCE	// feature test macro

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
	signed int len=-1, org=-1;
	int arg;
	int state=0;
	for(arg=1;arg<argc;arg++)
	{
		char *varg=argv[arg];
		if(varg[0]=='-')
		{
			if(strcmp(varg, "-l")==0)
				state=1;
			else if(strcmp(varg, "-o")==0)
				state=2;
			else if(strcmp(varg, "--scr")==0)
			{
				len=0x1b00;
				org=0x4000;
			}
		}
		signed int num=-1;
		sscanf(varg, "%d", &num);
		switch(state)
		{
			case 1:
				if(num>=0)
				{
					len=num;
					state=0;
				}
				else
				{
					fprintf(stderr, "objify: bad argument %s to -l\n", varg);
					return(EXIT_FAILURE);
				}
			break;
			case 2:
				if(num>=0)
				{
					org=num;
					state=0;
				}
				else
				{
					fprintf(stderr, "objify: bad argument %s to -o\n", varg);
					return(EXIT_FAILURE);
				}
			break;
		}
	}
	if(org>=0)
		printf("@%04X\n", org);
	if(len>=0)
		printf("*%04X\n", len);
	unsigned int pos=0;
	unsigned char cksum=0;
	while((len<0)||(pos<len))
	{
		signed int i=getchar();
		if(i==EOF)
		{
			if(len<0)
			{
				while(pos++%8)
					printf("$$ ");
				printf("== %02X\n", cksum);
				return(EXIT_SUCCESS);
			}
			fprintf(stderr, "objify: unexpected EOF (offset %04X, expected-length %04X)\n", pos, len);
			return(EXIT_FAILURE);
		}
		unsigned char c=i;
		printf("%02X ", c);
		cksum^=c;
		if(!(++pos%8))
		{
			printf("== %02X\n", cksum);
			cksum=0;
		}
	}
	if(pos%8)
	{
		while(pos++%8)
			printf("$$ ");
		printf("== %02X\n", cksum);
	}
	if(getchar()!=EOF)
	{
		fprintf(stderr, "objify: warning, excess bytes in input file\n");
	}
	return(EXIT_SUCCESS);
}
