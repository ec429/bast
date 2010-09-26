#include <stdio.h>
#include <string.h>
#include "version.h"

int main(void)
{
	char *vtxt=strdup(VERSION_TXT);
	char *spc=vtxt;
	while((spc=strchr(spc, ' ')))
		*spc='-';
	printf("%hhu.%hhu.%hhu%s%s\n", VERSION_MAJ, VERSION_MIN, VERSION_REV, VERSION_TXT[0]?"-":"", vtxt);
	return(0);
}
