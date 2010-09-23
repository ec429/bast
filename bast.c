/*
	bast - ZX Basic text to tape
	
	Copyright Edward Cree, 2010
	License: GNU GPL v3+
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

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
	int blines; // number of *actual* BASIC lines (as opposed to .labels, #directives etc.)
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

typedef struct
{
	int seg; // segment
	union // depends on segment.type
	{
		int line; // linenumber of BASIC line
		off_t offset; // offset of M/C label within segment
	}
	ptr; // pointer within that segment
	char *text; // label text
}
label;

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
void zxfloat(char *buf, double value);
bool isvalidlabel(char *text);
void addlabel(int *nlabels, label **labels, label lbl);

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
		fprintf(stderr, "bast: BASIC segment '%s', read %u physical lines\n", curr->name, curr->data.bas.nlines);
	}
	
	/* END: READ BASIC FILES */
	
	/* TODO: READ OBJECT FILES */
	
	/* TODO: fork the assembler for each #[r]asm/#endasm block */
	
	/* TOKENISE BASIC SEGMENTS */
	if(ninbas && outtype==TAPE)
	{
		int i;
		for(i=0;i<nsegs;i++)
		{
			if(data[i].type==BASIC)
			{
				data[i].data.bas.blines=0;
				fprintf(stderr, "bast: tokenising BASIC segment %s\n", data[i].name);
				int j;
				for(j=0;j<data[i].data.bas.nlines;j++)
				{
					err=false;
					fprintf(stderr, "bast: tokenising line %s\n", data[i].data.bas.basic[j].text);
					tokenise(&data[i].data.bas.basic[j], inbas, i);
					if(data[i].data.bas.basic[j].ntok) data[i].data.bas.blines++;
					if(err) return(EXIT_FAILURE);
				}
				fprintf(stderr, "bast: Tokenised BASIC segment %s (%u logical lines)\n", data[i].name, data[i].data.bas.blines);
			}
		}
	}
	/* END: TOKENISE BASIC SEGMENTS */
	
	/* LINKER & LABELS */
	// TODO PASS 1: Find labels, renumber labelled BASIC sources
	int nlabels=0;
	label * labels=NULL;
	int i;
	for(i=0;i<nsegs;i++)
	{
		fprintf(stderr, "bast: Linker (Pass 1): %s\n", data[i].name);
		switch(data[i].type)
		{
			case BASIC:;
				int num=0;
				if(data[i].data.bas.renum==1)
				{
					num=10;
					while(data[i].data.bas.blines*num>9999)
					{
						num--;
						if((num==7)||(num==9))
							num--;
					}
					if(!num)
					{
						fprintf(stderr, "bast: Renumber: Couldn't fit %s into 9999 lines\n", data[i].name);
						return(EXIT_FAILURE);
					}
					fprintf(stderr, "bast: Renumber: BASIC segment %s, spacing %u\n", data[i].name, num);
				}
				int dnum=num;
				int last=0;
				int j;
				for(j=0;j<data[i].data.bas.nlines;j++)
				{
					if(data[i].data.bas.basic[j].ntok)
					{
						if(num)
						{
							if(data[i].data.bas.renum!=1)
							{
								fprintf(stderr, "bast: Linker (Pass 1): Internal error (num!=0 but renum!=1), %s\n", data[i].name);
								return(EXIT_FAILURE);
							}
							data[i].data.bas.basic[j].number=num;
							num+=dnum;
						}
						else
						{
							if(data[i].data.bas.renum)
							{
								fprintf(stderr, "bast: Linker (Pass 1): Internal error (num==0 but renum!=0), %s\n", data[i].name);
								return(EXIT_FAILURE);
							}
							while(last<nlabels)
								labels[last++].ptr.line=data[i].data.bas.basic[j].number;
						}
					}
					else if(*data[i].data.bas.basic[j].text=='.')
					{
						if(isvalidlabel(data[i].data.bas.basic[j].text+1))
						{
							label lbl;
							lbl.text=strdup(data[i].data.bas.basic[j].text+1);
							lbl.seg=i;
							lbl.ptr.line=num;
							addlabel(&nlabels, &labels, lbl);
						}
					}
				}
				if(num) data[i].data.bas.renum=2;
			break;
			default:
				fprintf(stderr, "bast: Linker: Bad segment-type %u\n", data[i].type);
				return(EXIT_FAILURE);
			break;
		}
	}
	for(i=0;i<nsegs;i++)
	{
		fprintf(stderr, "bast: Linker (Pass 2): %s\n", data[i].name);
		switch(data[i].type)
		{
			case BASIC:;
				int j;
				for(j=0;j<data[i].data.bas.nlines;j++)
				{
					int k;
					for(k=0;k<data[i].data.bas.basic[j].ntok;k++)
					{
						if(data[i].data.bas.basic[j].tok[k].tok==TOKEN_LABEL)
						{
							int l;
							for(l=0;l<nlabels;l++)
							{
								// TODO limit label scope to this file & the files it has #imported
								if((data[labels[l].seg].type==BASIC) && (strcmp(data[i].data.bas.basic[j].tok[k].data, labels[l].text)==0))
								{
									data[i].data.bas.basic[j].tok[k].tok=TOKEN_ZXFLOAT;
									data[i].data.bas.basic[j].tok[k].data=(char *)malloc(16);
									sprintf(data[i].data.bas.basic[j].tok[k].data, "%u", labels[l].ptr.line);
									size_t s=strlen(data[i].data.bas.basic[j].tok[k].data);
									data[i].data.bas.basic[j].tok[k].data[s]=TOKEN_ZXFLOAT;
									zxfloat(data[i].data.bas.basic[j].tok[k].data+s+1, labels[l].ptr.line);
									break;
								}
							}
							if(l==nlabels)
							{
								fprintf(stderr, "bast: Linker: Undefined label %s\n\t"LOC"\n", data[i].data.bas.basic[j].tok[k].data, data[i].name, j);
							}
						}
					}
				}
			break;
			default:
				fprintf(stderr, "bast: Linker: Bad segment-type %u\n", data[i].type);
				return(EXIT_FAILURE);
			break;
		}
	}
	/* END: LINKER & LABELS */
	
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
			if(strchr("=<>[]()+-*/^,;:", c)) // Special munging rule: add a space before any =, < or >, [ or ], ( or ), +, -, *, /, ^, ,, ;, :
				append_char(&lout, &l, &i, ' ');
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
			char *curtok=NULL;
			while(*ptr)
			{
				while((*ptr==' ')||(*ptr=='\t'))
					ptr++;
				if(!*ptr)
					break;
				tl=0;
				init_char(&curtok, &l, &i);
				while(ptr[tl])
				{
					append_char(&curtok, &l, &i, ptr[tl++]);
					fprintf(stderr, "gettoken(%s)", curtok);
					token dat=gettoken(curtok);
					fprintf(stderr, "\t= %02X\n", dat.tok);
					if(dat.tok) // token is recognised?
					{
						b->ntok++;
						b->tok=(token *)realloc(b->tok, b->ntok*sizeof(token));
						b->tok[b->ntok-1]=dat;
						ptr+=tl;
						tl=0;
						if(curtok) free(curtok);
						curtok=NULL;
						if(dat.tok==0xEA) // REM token; eat the rest of the line (as token.data)
						{
							b->tok[b->ntok-1].data=strdup(ptr);
							ptr+=strlen(ptr);
						}
						break;
					}
				}
				if(!ptr[tl])
					break;
			}
			if(tl)
			{
				fprintf(stderr, "gettoken(%s\\n)", curtok);
				append_char(&curtok, &l, &i, '\n');
				token dat=gettoken(curtok);
				fprintf(stderr, "\t= %02X\n", dat.tok);
				if(dat.tok) // token is recognised?
				{
					b->ntok++;
					b->tok=(token *)realloc(b->tok, b->ntok*sizeof(token));
					b->tok[b->ntok-1]=dat;
				}
				else
				{
					fprintf(stderr, "bast: Failed to tokenise '%s'\n\t"LOC"\n", ptr, LOCARG);
					err=true;
				}
			}
			if(curtok) free(curtok);
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
			rv.data[sm-data]=0;
			rv.tok=TOKEN_STRING;
			return(rv);
		}
	}
	if(*data=='%')
	{
		char *sp=strpbrk(data, " \t\n");
		if(sp && !sp[1])
		{
			rv.data=strdup(data+1);
			rv.data[sp-data-1]=0;
			rv.tok=TOKEN_LABEL;
			return(rv);
		}
	}
	// test for number
	char *endptr;
	double num=strtod(data, &endptr);
	if(strchr(" \t\n", *endptr))
	{
		// 0x0E		ZX floating point number (full representation in token.data is (decimal)\0x0E(ZXfloat[5])
		rv.tok=TOKEN_ZXFLOAT;
		rv.data=(char *)malloc(endptr-data+6);
		strncpy(rv.data, data, endptr-data);
		rv.data[endptr-data]=TOKEN_ZXFLOAT;
		zxfloat(rv.data+1+(endptr-data), num);
	}
	// TODO: HEX, OCT
	if(strchr(" \t\n", data[strlen(data)-1]))
	{
		// assume it's a variable
		int i=0,s=0;
		while(data[i])
		{
			if(strchr(" \t\n", data[i]))
				break;
			if(!isalpha(data[i]))
			{
				if(data[i]=='$')
					s=1;
				else
					s=2;
				break;
			}
			i++;
		}
		if((!s) || ((s==1) && (i==2)))
		{
			rv.tok=s?TOKEN_VARSTR:TOKEN_VAR;
			rv.data=strdup(data);
			rv.data[i]=0;
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

void zxfloat(char *buf, double value)
{
	if((labs(value-floor(value+0.5))<1e-12) && (fabs(value)<65535.5))
	{
		int i=floor(value+0.5);
		// "small integer"
		// 00 {00|FF}sign LSB MSB 00
		buf[0]=0;
		buf[1]=(i<0)?0xFF:0;
		buf[2]=abs(i);
		buf[3]=abs(i>>8);
		buf[4]=0;
	}
	else
	{
		// 4mantissa + 1exponent
		// m*2^(e-128)
		int ex=1+floor(log(fabs(value))/M_LN2);
		double mant=fabs(value)*exp(-ex*M_LN2);
		long mantissa=floor(mant*exp(32*M_LN2)+0.5);
		buf[0]=((mantissa>>24)&0x7F)|((value<0)?0x80:0);
		buf[1]=mantissa>>16;
		buf[2]=mantissa>>8;
		buf[3]=mantissa;
		buf[4]=ex+128;
	}
}

bool isvalidlabel(char *text)
{
	if(!isalpha(*text))
		return(false);
	size_t s=strspn(text, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
	return(!text[s]);
}

void addlabel(int *nlabels, label **labels, label lbl)
{
	int nl=(*nlabels)+1;
	label *ll=(label *)realloc(*labels, nl*sizeof(label));
	if(ll)
	{
		*nlabels=nl;
		*labels=ll;
		ll[nl-1]=lbl;
	}
}
