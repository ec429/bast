/*
	bast - ZX Basic text to tape
	
	Copyright Edward Cree, 2010
	License: GNU GPL v3+
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "tokens.h"

typedef struct
{
	int sline; // source linenumber (for diagnostics)
	int number; // line number (filled in either by tokeniser or by renum); if <1, NZ if should be renumbered
	char *text; // raw text
	int ntok; // number of tokens in line
	token *tok; // results of tokenisation
}
basline;

typedef struct
{
	enum {BYTE, LBL, LBM} type;
	unsigned char byte;
	// TODO pointer to line if LBL or LBM
}
bin_byte;

typedef struct
{
	int nbytes;
	bin_byte *bytes;
}
bin_seg;

typedef struct
{
	int nlines;
	basline *basic;
	int line; // #pragma line? 0:NO, >0:linenumber, <0:label
	char *lline; // label for #pragma line if line<0 
	int renum; // #pragma renum?  0:NO, 1:YES but not done yet, 2:YES and it's been done now
}
bas_seg;

typedef struct
{
	enum {NONE, BASIC, BINARY} type;
	char *name;
	union {bas_seg bas; bin_seg bin;} data;
}
segment;

#define LOC		"%s:%u"
#define LOCARG	inbas[fbas], fline

bool err=false;

char * fgetl(FILE *fp);
void init_char(char **buf, int *l, int *i);
void append_char(char **buf, int *l, int *i, char c);
int addinbas(int *ninbas, char ***inbas, char *arg);
int addbasline(int *nlines, basline **basic, char *line);
segment *addsegment(int *nsegs, segment **data);
void basfree(basline b);
void tokenise(basline *b, char **inbas, int fbas);
token gettoken(char *data);

int main(int argc, char *argv[])
{
	/* SET UP TABLES ETC */
	mktoktbl();
	/* END: SET UP TABLES ETC */
	
	/* PARSE ARGUMENTS */
	int ninbas=0;
	char **inbas=NULL;
	int ninobj=0;
	char **inobj=NULL;
	enum {NONE, OBJ, TAPE} outtype=NONE;
	char *outfile=NULL;
	int arg;
	int state=0;
	for(arg=1;arg<argc;arg++)
	{
		char *varg=argv[arg];
		if(strcmp(varg, "-")==0)
			varg="/dev/stdin";
		if(*varg=='-')
		{
			if(strcmp(varg, "-b")==0)
				state=1;
			else if(strcmp(varg, "-t")==0)
				state=2;
			else if(strcmp(varg, "-W")==0)
				state=3;
			else
			{
				fprintf(stderr, "bast: No such option %s\n", varg);
				return(EXIT_FAILURE);
			}
		}
		else
		{
			switch(state)
			{
				case 0:
				case 1:
					if(addinbas(&ninbas, &inbas, varg))
					{
						fprintf(stderr, "bast: Internal error: Failed to add %s to inbas list\n", varg);
						return(EXIT_FAILURE);
					}
					state=0;
				break;
				case 2:
					outtype=TAPE;
					outfile=strdup(varg);
					state=0;
				break;
				case 3:
					if(strcmp("all", varg)==0)
					{
						state=0; // we have no warnings so far, so we'll just ignore -W all
					}
				break;
				default:
					fprintf(stderr, "bast: Internal error: Bad state %u in args\n", state);
					return(EXIT_FAILURE);
				break;
			}
		}
	}
	
	/* END: PARSE ARGUMENTS */
	
	if(!(ninbas||ninobj))
	{
		fprintf(stderr, "bast: No input files specified\n");
		return(EXIT_FAILURE);
	}
	
	if((outtype==NONE)||!outfile)
	{
		fprintf(stderr, "bast: No output file specified\n");
		return(EXIT_FAILURE);
	}
	
	int nsegs=0;
	segment * data=NULL;
	
	/* READ BASIC FILES */
	
	if(ninbas&&!inbas)
	{
		fprintf(stderr, "bast: Internal error: ninbas!=0 and inbas is NULL\n");
		return(EXIT_FAILURE);
	}
	
	int fbas;
	for(fbas=0;fbas<ninbas;fbas++)
	{
		int fline=0;
		int dfl=0;
		FILE *fp=fopen(inbas[fbas], "r");
		if(!fp)
		{
			fprintf(stderr, "bast: Failed to open input file %s\n", inbas[fbas]);
			return(EXIT_FAILURE);
		}
		segment *curr=addsegment(&nsegs, &data);
		if(!curr)
		{
			fprintf(stderr, "bast: Internal error: failed to add segment for file %s\n", inbas[fbas]);
			return(EXIT_FAILURE);
		}
		curr->name=(char *)malloc(10);
		sprintf(curr->name, "bas%u", fbas);
		curr->type=BASIC;
		curr->data.bas.nlines=0;
		curr->data.bas.basic=NULL;
		curr->data.bas.line=0;
		curr->data.bas.lline=NULL;
		curr->data.bas.renum=0;
		while(!feof(fp))
		{
			char *line=fgetl(fp);
			if(line)
			{
				fline+=dfl+1;
				dfl=0;
				if(*line)
				{
					while(line[strlen(line)-1]=='\\') // line splicing
					{
						char *second=fgetl(fp);
						if(!second) break;
						dfl++;
						if(!*second) break;
						line[strlen(line)-1]=0;
						char *splice=(char *)realloc(line, strlen(line)+strlen(second)+1);
						if(!splice) break;
						strcat(splice, second);
						free(second);
					}
					if(addbasline(&curr->data.bas.nlines, &curr->data.bas.basic, line))
					{
						fprintf(stderr, "bast: Internal error: Failed to store line as text\n\t"LOC"\n", LOCARG);
						return(EXIT_FAILURE);
					}
					curr->data.bas.basic[curr->data.bas.nlines-1].sline=fline;
					if(*line=='#')
					{
						char *cmd=strtok(line, " ");
						if(cmd)
						{
							if(strcmp(cmd, "#pragma")==0)
							{
								char *prgm=strtok(NULL, " ");
								if(prgm)
								{
									if(strcmp(prgm, "name")==0)
									{
										char *basname=strtok(NULL, "");
										if(basname)
										{
											if(curr->name) free(curr->name);
											curr->name=strdup(basname);
										}
									}
									else if(strcmp(prgm, "line")==0)
									{
										char *pline=strtok(NULL, "");
										if(pline)
										{
											int val;
											if(sscanf(pline, "%u", &val)==1)
											{
												curr->data.bas.line=val;
											}
											else
											{
												curr->data.bas.line=-1;
												curr->data.bas.lline=strdup(pline);
											}
										}
										else
										{
											fprintf(stderr, "bast: #pragma line missing argument\n\t"LOC"\n", LOCARG);
										}
									}
									else if(strcmp(prgm, "renum")==0)
									{
										curr->data.bas.renum=1;
									}
									else
									{
										fprintf(stderr, "bast: Warning: #pragma %s not recognised (ignoring)\n\t"LOC"\n", prgm, LOCARG);
									}
								}
								else
								{
									fprintf(stderr, "bast: #pragma without identifier\n\t"LOC"\n", LOCARG);
									return(EXIT_FAILURE);
								}
							}
							else
							{
								fprintf(stderr, "bast: Unrecognised directive %s\n\t"LOC"\n", cmd, LOCARG);
								return(EXIT_FAILURE);
							}
						}
					}
				}
				free(line);
			}
		}
		fprintf(stderr, "bast: BASIC segment '%s', read %u lines (logical)\n", curr->name, curr->data.bas.nlines);
	}
	
	/* END: READ BASIC FILES */
	
	/* TODO: READ OBJECT FILES */
	
	/* TODO: fork the assembler for each #[r]asm/#endasm block */
	
	/* TOKENISE BASIC SEGMENTS */
	if(ninbas)
	{
		int i;
		for(i=0;i<nsegs;i++)
		{
			if(data[i].type==BASIC)
			{
				fprintf(stderr, "bast: tokenising BASIC segment %s\n", data[i].name);
				int j;
				for(j=0;j<data[i].data.bas.nlines;j++)
				{
					err=false;
					fprintf(stderr, "bast: tokenising line %s\n", data[i].data.bas.basic[j].text);
					tokenise(&data[i].data.bas.basic[j], inbas, i);
					if(err) return(EXIT_FAILURE);
				}
				fprintf(stderr, "bast: Tokenised BASIC segment %s\n", data[i].name);
			}
		}
	}
	/* END: TOKENISE BASIC SEGMENTS */
	
	return(EXIT_SUCCESS);
}

char * fgetl(FILE *fp)
{
	char * lout;
	int l,i;
	init_char(&lout, &l, &i);
	signed int c;
	while(!feof(fp))
	{
		c=fgetc(fp);
		if((c==EOF)||(c=='\n'))
			break;
		if(c!=0)
		{
			append_char(&lout, &l, &i, c);
		}
	}
	return(lout);
}

void append_char(char **buf, int *l, int *i, char c)
{
	if(!((c==0)||(c==EOF)))
	{
		if(*buf)
		{
			(*buf)[(*i)++]=c;
			char *nbuf=*buf;
			if((*i)>=(*l))
			{
				*l=*i*2;
				nbuf=(char *)realloc(*buf, *l);
			}
			if(nbuf)
			{
				*buf=nbuf;
				(*buf)[*i]=0;
			}
			else
			{
				free(*buf);
				init_char(buf, l, i);
			}
		}
		else
		{
			init_char(buf, l, i);
			append_char(buf, l, i, c);
		}
	}
}

void init_char(char **buf, int *l, int *i)
{
	*l=80;
	*buf=(char *)malloc(*l);
	(*buf)[0]=0;
	*i=0;
}

int addinbas(int *ninbas, char ***inbas, char *arg)
{
	int nb=(*ninbas)+1;
	char **ib=(char **)realloc(*inbas, nb*sizeof(char *));
	if(ib)
	{
		*ninbas=nb;
		*inbas=ib;
		ib[nb-1]=strdup(arg);
		return(0);
	}
	else
	{
		return(1);
	}
}

int addbasline(int *nlines, basline **basic, char *line)
{
	int nl=(*nlines)+1;
	basline *nb=(basline *)realloc(*basic, nl*sizeof(basline));
	if(nb)
	{
		*nlines=nl;
		*basic=nb;
		nb[nl-1].sline=0;
		nb[nl-1].number=0;
		nb[nl-1].text=strdup(line);
		nb[nl-1].ntok=0;
		nb[nl-1].tok=NULL;
		return(0);
	}
	else
	{
		return(1);
	}
}

segment *addsegment(int *nsegs, segment **data)
{
	int ns=(*nsegs)+1;
	segment *nd=(segment *)realloc(*data, ns*sizeof(segment));
	if(nd)
	{
		*nsegs=ns;
		*data=nd;
		nd[ns-1].type=NONE;
		nd[ns-1].name=NULL;
		return(&nd[ns-1]);
	}
	else
	{
		return(NULL);
	}
}

void basfree(basline b)
{
	if(b.text) free(b.text);
	if(b.tok) free(b.tok);
}

void tokenise(basline *b, char **inbas, int fbas)
{
	if(b)
	{
		int fline=b->sline;
		if(b->tok) free(b->tok);
		b->tok=NULL;
		b->ntok=0;
		if(b->text && !strchr("#.\n", *b->text))
		{
			char *ptr=b->text;
			int tl=0;
			int l=0,i;
			while(*ptr)
			{
				while((*ptr==' ')||(*ptr=='\t'))
					ptr++;
				if(!*ptr)
					break;
				tl=0;
				char *curtok;
				init_char(&curtok, &l, &i);
				while(ptr[tl])
				{
					append_char(&curtok, &l, &i, ptr[tl++]);
					fprintf(stderr, "gettoken(%s)", curtok);
					token dat=gettoken(curtok);
					fprintf(stderr, "\t= %02X\n", dat.tok);
					if(dat.tok) // token is recognised?
					{
						ptr+=tl;
						tl=0;
						break;
					}
				}
				if(!ptr[tl])
					break;
				if(curtok) free(curtok);
			}
			if(tl)
			{
				fprintf(stderr, "bast: Failed to tokenise '%s'\n\t"LOC"\n", ptr, LOCARG);
				err=true;
			}
		}
	}
}

token gettoken(char *data)
{
	token rv;
	rv.text=data;
	rv.tok=0;
	rv.data=NULL;
	if(*data=='"')
	{
		char *sm=strchr(data+1, '"');
		if(sm && !sm[1])
		{
			rv.data=strdup(data+1);
			sm=strchr(rv.data, '"');
			if(sm) *sm=0;
			rv.tok=0xF;
			return(rv);
		}
	}
	int i;
	for(i=0;i<ntokens;i++)
	{
		if(strcasecmp(data, tokentable[i].text)==0)
		{
			rv.tok=tokentable[i].tok;
			return(rv);
		}
	}
	return(rv);
}
