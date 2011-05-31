/*
	bast - ZX Basic text to tape
	
	Copyright Edward Cree, 2010
	License: GNU GPL v3+
*/

#define _GNU_SOURCE	// feature test macro

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "tokens.h"
#include "version.h"

#define VERSION_MSG " %s %hhu.%hhu.%hhu%s%s\n\
 Copyright (C) 2010 Edward Cree.\n\
 License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>\n\
 This is free software: you are free to change and redistribute it.\n\
 There is NO WARRANTY, to the extent permitted by law.\n\
 Compiler was %s\n", "bast", VERSION_MAJ, VERSION_MIN, VERSION_REV, VERSION_TXT[0]?"-":"", VERSION_TXT, CC_VERSION

#define max(a,b)	((a)>(b)?(a):(b))
#define min(a,b)	((a)>(b)?(b):(a))

typedef struct
{
	int sline; // source linenumber (for diagnostics)
	int number; // line number (filled in either by tokeniser or by renum); if <1, NZ if should be renumbered
	char *text; // raw text
	int ntok; // number of tokens in line
	token *tok; // results of tokenisation
	off_t offset; // offset of start of text within BASIC segment.  Is relative to 0x5CCB, typically
}
basline;

typedef struct
{
	enum {BNONE, BYTE, LBL, LBM} type;
	unsigned char byte;
	// TODO pointer to line if LBL or LBM
}
bin_byte;

typedef struct
{
	int nbytes;
	bin_byte *bytes;
	int org;
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
	int rnstart;
	int rnoffset;
	int rnend;
	char *block; // data block
	ssize_t blen; // length of block
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
	int sline; // offset of BASIC line within bas_seg
	int line; // linenumber of BASIC line
	off_t offset; // offset of BINARY label within bin_seg
	char *text; // label text
}
label;

#define LOC		"%s:%u"
#define LOCARG	inbas[fbas], fline

bool err=false;

char * fgetl(FILE *fp);
void init_char(char **buf, int *l, int *i);
void append_char(char **buf, int *l, int *i, char c);
void append_str(char **buf, int *l, int *i, char *str);
int addinbas(int *ninbas, char ***inbas, char *arg);
int addbasline(int *nlines, basline **basic, char *line);
segment *addsegment(int *nsegs, segment **data);
void basfree(basline b);
void tokenise(basline *b, char **inbas, int fbas, int renum);
token gettoken(char *data, int *bt);
void zxfloat(char *buf, double value);
bool isvalidlabel(char *text);
void addlabel(int *nlabels, label **labels, label lbl);
void buildbas(bas_seg *bas, bool write);
void bin_load(char *fname, FILE *fp, bin_seg * buf, char **name);

bool debug=false;
bool Wobjlen=false;
bool Wsebasic=true;
bool Wembeddednewline=true;
bool Ocutnumbers=false;

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
	bool emu=false;
	int arg;
	int state=0;
	for(arg=1;arg<argc;arg++)
	{
		char *varg=argv[arg];
		if(strcmp(varg, "-")==0)
			varg="/dev/stdin";
		if(*varg=='-')
		{
			if(strcmp(varg, "-V")==0)
			{
				printf(VERSION_MSG);
				return(EXIT_SUCCESS);
			}
			else if(strcmp(varg, "--emu")==0)
			{
				emu=true;
			}
			else if(strcmp(varg, "--no-emu")==0)
			{
				emu=false;
			}
			else if(strcmp(varg, "--debug")==0)
			{
				debug=true;
			}
			else if(strcmp(varg, "--no-debug")==0)
			{
				debug=false;
			}
			else if(strcmp(varg, "-b")==0)
				state=1;
			else if(strcmp(varg, "-l")==0)
				state=7;
			else if(strcmp(varg, "-t")==0)
				state=2;
			else if(strcmp(varg, "-W")==0)
				state=3;
			else if(strcmp(varg, "-W-")==0)
				state=4;
			else if(strcmp(varg, "-O")==0)
				state=5;
			else if(strcmp(varg, "-O-")==0)
				state=6;
			else
			{
				fprintf(stderr, "bast: No such option %s\n", varg);
				return(EXIT_FAILURE);
			}
		}
		else
		{
			bool flag=false;
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
				case 7:
					if(addinbas(&ninobj, &inobj, varg))
					{
						fprintf(stderr, "bast: Internal error: Failed to add %s to inobj list\n", varg);
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
					flag=true; // fallthrough
				case 4:
					if(strcmp("all", varg)==0)
					{
						Wobjlen=flag;
						state=0;
					}
					else if(strcmp("object-length", varg)==0)
					{
						Wobjlen=flag;
						state=0;
					}
					else if(strcmp("se-basic", varg)==0)
					{
						Wsebasic=flag;
						state=0;
					}
					else if(strcmp("embedded-newline", varg)==0)
					{
						Wembeddednewline=flag;
						state=0;
					}
				break;
				case 5:
					flag=true; // fallthrough
				case 6:
					if(strcmp("cut-numbers", varg)==0)
					{
						Ocutnumbers=flag;
						state=0;
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
		curr->data.bas.block=NULL;
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
						if(!second) continue;
						dfl++;
						if(!*second)
						{
							free(second);
							continue;
						}
						line[strlen(line)-1]=0;
						char *splice=(char *)realloc(line, strlen(line)+strlen(second)+2);
						if(!splice)
						{
							free(second);
							continue;
						}
						line=splice;
						strcat(splice, second);
						free(second);
					}
					if(Wembeddednewline && strchr(line, '\x0D')) // 0x0D is newline in ZX BASIC
					{
						fprintf(stderr, "bast: Warning: embedded newline (\\0D) in ZX Basic line\n\t"LOC"\n", LOCARG);
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
											unsigned int val;
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
											fprintf(stderr, "bast: Warning: #pragma line missing argument\n\t"LOC"\n", LOCARG);
										}
									}
									else if(strcmp(prgm, "renum")==0)
									{
										curr->data.bas.renum=1;
										curr->data.bas.rnstart=0;
										curr->data.bas.rnoffset=0;
										curr->data.bas.rnend=0;
										char *arg=strtok(NULL, " ");
										while(arg)
										{
											unsigned int val=0;
											if(*arg)
												sscanf(arg+1, "%u", &val);
											switch(*arg)
											{
												case '=':
													curr->data.bas.rnstart=val;
												break;
												case '+':
													curr->data.bas.rnoffset=val;
												break;
												case '-':
													curr->data.bas.rnend=val;
												break;
												default:
													fprintf(stderr, "bast: Warning: #pragma renum bad argument %s\n\t"LOC"\n", arg, LOCARG);
												break;
											}
											arg=strtok(NULL, " ");
										}
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
							else if(strcmp(cmd, "##")==0)
							{
								// comment, ignore
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
	
	/* READ OBJECT FILES */
	int fobj;
	for(fobj=0;fobj<ninobj;fobj++)
	{
		FILE *fp=fopen(inobj[fobj], "r");
		if(!fp)
		{
			fprintf(stderr, "bast: Failed to open input file %s\n", inobj[fobj]);
			return(EXIT_FAILURE);
		}
		segment *curr=addsegment(&nsegs, &data);
		if(!curr)
		{
			fprintf(stderr, "bast: Internal error: failed to add segment for file %s\n", inobj[fobj]);
			return(EXIT_FAILURE);
		}
		curr->name=(char *)malloc(10);
		sprintf(curr->name, "bin%u", fobj);
		curr->type=BINARY;
		err=false;
		bin_load(inobj[fobj], fp, &curr->data.bin, &curr->name);
		if(err)
		{
			fprintf(stderr, "bast: Failed to load BINARY segment from file %s\n", inobj[fobj]);
			return(EXIT_FAILURE);
		}
	}
	/* END: READ OBJECT FILES */
	
	/* TODO: fork the assembler for each #[r]asm/#endasm block */
	
	/* TOKENISE BASIC SEGMENTS */
	if(ninbas)
	{
		int i;
		for(i=0;i<nsegs;i++)
		{
			if(data[i].type==BASIC)
			{
				data[i].data.bas.blines=0;
				fprintf(stderr, "bast: Tokenising BASIC segment %s\n", data[i].name);
				int j;
				for(j=0;j<data[i].data.bas.nlines;j++)
				{
					err=false;
					if(debug) fprintf(stderr, "bast: tokenising line %s\n", data[i].data.bas.basic[j].text);
					tokenise(&data[i].data.bas.basic[j], inbas, i, data[i].data.bas.renum);
					if(data[i].data.bas.basic[j].ntok) data[i].data.bas.blines++;
					if(err) return(EXIT_FAILURE);
				}
				fprintf(stderr, "bast: Tokenised BASIC segment %s (%u logical lines)\n", data[i].name, data[i].data.bas.blines);
			}
		}
	}
	/* END: TOKENISE BASIC SEGMENTS */
	
	/* LINKER & LABELS */
	// PASS 1: Find labels, renumber labelled BASIC sources, load in !links as attached bin_segs
	int nlabels=0;
	label * labels=NULL;
	int i;
	for(i=0;i<nsegs;i++)
	{
		fprintf(stderr, "bast: Linker (Pass 1): %s\n", data[i].name);
		switch(data[i].type)
		{
			case BASIC:;
				int num=0,dnum=0;
				if(data[i].data.bas.renum==1)
				{
					dnum=data[i].data.bas.rnoffset?data[i].data.bas.rnoffset:10;
					int end=data[i].data.bas.rnend?data[i].data.bas.rnend:9999;
					while(data[i].data.bas.blines*dnum>end)
					{
						dnum--;
						if((dnum==7)||(dnum==9))
							dnum--;
					}
					if(!dnum)
					{
						fprintf(stderr, "bast: Renumber: Couldn't fit %s into available lines\n", data[i].name);
						return(EXIT_FAILURE);
					}
					num=data[i].data.bas.rnstart?data[i].data.bas.rnstart:dnum;
					fprintf(stderr, "bast: Renumber: BASIC segment %s, start %u, spacing %u, end <=%u\n", data[i].name, num, dnum, end);
				}
				int dl;
				init_char(&data[i].data.bas.block, &dl, (int *)&data[i].data.bas.blen);
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
							{
								labels[last].sline=j;
								labels[last++].line=data[i].data.bas.basic[j].number;
							}
						}
						int k;
						for(k=0;k<data[i].data.bas.basic[j].ntok;k++)
						{
							if(data[i].data.bas.basic[j].tok[k].tok==TOKEN_RLINK)
							{
								if(data[i].data.bas.basic[j].tok[k].data)
								{
									FILE *fp=fopen(data[i].data.bas.basic[j].tok[k].data, "rb");
									if(fp)
									{
										data[i].data.bas.basic[j].tok[k].data2=(char *)malloc(sizeof(bin_seg));
										err=false;
										bin_load(data[i].data.bas.basic[j].tok[k].data, fp, (bin_seg *)data[i].data.bas.basic[j].tok[k].data2, NULL);
										if(err)
										{
											fprintf(stderr, "bast: Linker: failed to attach BINARY segment\n\t%s:%u\n", data[i].name, j);
											return(EXIT_FAILURE);
										}
									}
									else
									{
										fprintf(stderr, "bast: Linker: failed to open rlinked file %s\n\t%s:%u\n", data[i].data.bas.basic[j].tok[k].data, data[i].name, j);
										return(EXIT_FAILURE);
									}
								}
								else
								{
									fprintf(stderr, "bast: Linker: Internal error: TOKEN_RLINK without filename\n\t%s:%u", data[i].name, j);
									return(EXIT_FAILURE);
								}
							}
						}
					}
					else if(*data[i].data.bas.basic[j].text=='.')
					{
						if(isvalidlabel(data[i].data.bas.basic[j].text+1))
						{
							label lbl;
							lbl.text=strdup(data[i].data.bas.basic[j].text+1);
							lbl.seg=i;
							lbl.line=num;
							lbl.sline=j;
							addlabel(&nlabels, &labels, lbl);
						}
					}
				}
				buildbas(&data[i].data.bas, false);
				if(data[i].data.bas.blen==-1)
				{
					fprintf(stderr, "bast: Failed to link BASIC segment %s\n", data[i].name);
					return(EXIT_FAILURE);
				}
				if(data[i].data.bas.renum) data[i].data.bas.renum=2;
			break;
			case BINARY:
				// TODO: export symbol table (we don't have symbols in object files yet)
				// Nothing else on pass 1
			break;
			default:
				fprintf(stderr, "bast: Linker: Internal error: Bad segment-type %u\n", data[i].type);
				return(EXIT_FAILURE);
			break;
		}
	}
	// PASS 2: Replace labels with the linenumbers/addresses to which they point
	for(i=0;i<nsegs;i++)
	{
		fprintf(stderr, "bast: Linker (Pass 2): %s\n", data[i].name);
		switch(data[i].type)
		{
			case BASIC:
				if(data[i].data.bas.line<0)
				{
					if(!data[i].data.bas.lline)
					{
						fprintf(stderr, "bast: Linker: Internal error: line<0 but lline=NULL, %s\n", data[i].name);
						return(EXIT_FAILURE);
					}
					int l;
					for(l=0;l<nlabels;l++)
					{
						// TODO limit label scope to this file & the files it has #imported
						if((data[labels[l].seg].type==BASIC) && (strcmp(data[i].data.bas.lline, labels[l].text)==0))
						{
							data[i].data.bas.line=labels[l].line;
							break;
						}
					}
					if(l==nlabels)
					{
						fprintf(stderr, "bast: Linker: Undefined label %s\n\t%s:#pragma line\n", data[i].data.bas.lline, data[i].name);
						return(EXIT_FAILURE);
					}
				}
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
									if(debug) fprintf(stderr, "bast: Linker: expanded %%%s", data[i].data.bas.basic[j].tok[k].data);
									if(data[i].data.bas.basic[j].tok[k].index)
									{
										if(debug) fprintf(stderr, "%s%02x", data[i].data.bas.basic[j].tok[k].index>0?"+":"-", abs(data[i].data.bas.basic[j].tok[k].index));
									}
									data[i].data.bas.basic[j].tok[k].tok=TOKEN_ZXFLOAT;
									if(Ocutnumbers)
									{
										data[i].data.bas.basic[j].tok[k].data=strdup(".");
										if(debug) fprintf(stderr, " to %u (cut)\n", labels[l].line+data[i].data.bas.basic[j].tok[k].index);
									}
									else
									{
										data[i].data.bas.basic[j].tok[k].data=(char *)malloc(6);
										sprintf(data[i].data.bas.basic[j].tok[k].data, "%05u", labels[l].line+data[i].data.bas.basic[j].tok[k].index);
										if(debug) fprintf(stderr, " to %s\n", data[i].data.bas.basic[j].tok[k].data);
									}
									data[i].data.bas.basic[j].tok[k].data2=(char *)malloc(6);
									zxfloat(data[i].data.bas.basic[j].tok[k].data2, labels[l].line+data[i].data.bas.basic[j].tok[k].index);
									break;
								}
							}
							if(l==nlabels)
							{
								fprintf(stderr, "bast: Linker: Undefined label %s\n\t"LOC"\n", data[i].data.bas.basic[j].tok[k].data, data[i].name, j);
								return(EXIT_FAILURE);
							}
						}
						else if(data[i].data.bas.basic[j].tok[k].tok==TOKEN_PTRLBL)
						{
							int l;
							for(l=0;l<nlabels;l++)
							{
								// TODO limit label scope to this file & the files it has #imported
								if((data[labels[l].seg].type==BASIC) && (strcmp(data[i].data.bas.basic[j].tok[k].data, labels[l].text)==0))
								{
									if(debug) fprintf(stderr, "bast: Linker: expanded @%s", data[i].data.bas.basic[j].tok[k].data);
									if(data[i].data.bas.basic[j].tok[k].index)
									{
										if(debug) fprintf(stderr, "%s%02x", data[i].data.bas.basic[j].tok[k].index>0?"+":"-", abs(data[i].data.bas.basic[j].tok[k].index));
									}
									data[i].data.bas.basic[j].tok[k].tok=TOKEN_ZXFLOAT;
									if(Ocutnumbers)
									{
										data[i].data.bas.basic[j].tok[k].data=strdup(".");
										if(debug) fprintf(stderr, " to %u (cut)\n", (unsigned int)data[labels[l].seg].data.bas.basic[labels[l].sline].offset+data[i].data.bas.basic[j].tok[k].index);
									}
									else
									{
										data[i].data.bas.basic[j].tok[k].data=(char *)malloc(6);
										sprintf(data[i].data.bas.basic[j].tok[k].data, "%05u", (unsigned int)data[labels[l].seg].data.bas.basic[labels[l].sline].offset+data[i].data.bas.basic[j].tok[k].index);
										if(debug) fprintf(stderr, " to %s\n", data[i].data.bas.basic[j].tok[k].data);
									}
									data[i].data.bas.basic[j].tok[k].data2=(char *)malloc(6);
									zxfloat(data[i].data.bas.basic[j].tok[k].data2, data[labels[l].seg].data.bas.basic[labels[l].sline].offset+data[i].data.bas.basic[j].tok[k].index);
									break;
								}
							}
							if(l==nlabels)
							{
								fprintf(stderr, "bast: Linker: Undefined label %s\n\t"LOC"\n", data[i].data.bas.basic[j].tok[k].data, data[i].name, j);
								return(EXIT_FAILURE);
							}
						}
					}
				}
			break;
			case BINARY:
				if(data[i].data.bin.nbytes)
				{
					int j;
					for(j=0;j<data[i].data.bin.nbytes;j++)
					{
						switch(data[i].data.bin.bytes[j].type)
						{
							case BYTE:
								// do nothing
							break;
							// TODO LBL, LBM (labelpointer parsing)
							default:
								fprintf(stderr, "bast: Linker: Bad byte-type %u\n\t%s+0x%04X\n", data[i].data.bin.bytes[j].type, data[i].name, j);
								return(EXIT_FAILURE);
							break;
						}
					}
				}
			break;
			default:
				fprintf(stderr, "bast: Linker: Internal error: Bad segment-type %u\n", data[i].type);
				return(EXIT_FAILURE);
			break;
		}
	}
	fprintf(stderr, "bast: Linker passed all segments\n");
	/* END: LINKER & LABELS */
	
	/* CREATE OUTPUT */
	switch(outtype)
	{
		case TAPE:
			fprintf(stderr, "bast: Creating TAPE output\n");
			if(nsegs)
			{
				FILE *fout=fopen(outfile, "wb");
				if(!fout)
				{
					fprintf(stderr, "bast: Could not open output file %s for writing!\n", outfile);
					return(EXIT_FAILURE);
				}
				int i;
				for(i=0;i<nsegs;i++)
				{
					// write header
					fputc(0x13, fout);
					fputc(0x00, fout);
					unsigned char cksum=0;
					fputc(0x00, fout); // HEADER
					int j;
					char name[10];
					switch(data[i].type)
					{
						case BASIC:
							fputc(0, fout); // PROGRAM
							memset(name, ' ', 10);
							memcpy(name, data[i].name, min(10, strlen(data[i].name)));
							for(j=0;j<10;j++)
							{
								fputc(name[j], fout);
								cksum^=name[j];
							}
							buildbas(&data[i].data.bas, true);
							if(data[i].data.bas.blen==-1)
							{
								fprintf(stderr, "bast: Failed to link BASIC segment %s\n", data[i].name);
								return(EXIT_FAILURE);
							}
							fputc(data[i].data.bas.blen, fout);
							cksum^=data[i].data.bas.blen&0xFF;
							fputc(data[i].data.bas.blen>>8, fout);
							cksum^=data[i].data.bas.blen>>8;
							if(data[i].data.bas.line) // Parameter 1 = autostart line
							{
								fputc(data[i].data.bas.line, fout);
								cksum^=data[i].data.bas.line&0xFF;
								fputc(data[i].data.bas.line>>8, fout);
								cksum^=data[i].data.bas.line>>8;
							}
							else // Parameter 1 = 0xFFFF
							{
								fputc(0xFF, fout);
								fputc(0xFF, fout);
							}
							// Parameter 2 = data[i].data.bas.blen
							fputc(data[i].data.bas.blen, fout);
							cksum^=data[i].data.bas.blen&0xFF;
							fputc(data[i].data.bas.blen>>8, fout);
							cksum^=data[i].data.bas.blen>>8;
							fputc(cksum, fout);
							// write data block
							fputc((data[i].data.bas.blen+2), fout);
							fputc((data[i].data.bas.blen+2)>>8, fout);
							fputc(0xFF, fout); // DATA
							cksum=0xFF;
							for(j=0;j<data[i].data.bas.blen;j++)
							{
								fputc(data[i].data.bas.block[j], fout);
								cksum^=data[i].data.bas.block[j];
							}
							fputc(cksum, fout);
							free(data[i].data.bas.block);
						break;
						case BINARY:
							fputc(3, fout); // CODE
							cksum^=3;
							memset(name, ' ', 10);
							memcpy(name, data[i].name, min(10, strlen(data[i].name)));
							for(j=0;j<10;j++)
							{
								fputc(name[j], fout);
								cksum^=name[j];
							}
							fputc(data[i].data.bin.nbytes, fout);
							cksum^=data[i].data.bin.nbytes&0xFF;
							fputc(data[i].data.bin.nbytes>>8, fout);
							cksum^=data[i].data.bin.nbytes>>8;
							// Parameter 1 = address
							fputc(data[i].data.bin.org, fout);
							cksum^=data[i].data.bin.org&0xFF;
							fputc(data[i].data.bin.org>>8, fout);
							cksum^=data[i].data.bin.org>>8;
							// Parameter 2 = 0x8000
							fputc(0x00, fout);
							fputc(0x80, fout);
							cksum^=0x80;
							fputc(cksum, fout);
							// write data block
							fputc((data[i].data.bin.nbytes+2), fout);
							fputc((data[i].data.bin.nbytes+2)>>8, fout);
							fputc(0xFF, fout); // DATA
							cksum=0xFF;
							for(j=0;j<data[i].data.bin.nbytes;j++)
							{
								fputc(data[i].data.bin.bytes[j].byte, fout);
								cksum^=data[i].data.bin.bytes[j].byte;
							}
							fputc(cksum, fout);
							free(data[i].data.bin.bytes);
						break;
						default:
							fprintf(stderr, "bast: Internal error: Don't know how to make TAPE output of segment type %u\n", data[i].type);
							return(EXIT_FAILURE);
						break;
					}
					
					fprintf(stderr, "bast: Wrote segment %s\n", data[i].name);
				}
				fclose(fout);
			}
			else
			{
				fprintf(stderr, "bast: There are no segments to write!\n");
				return(EXIT_FAILURE);
			}
		break;
		default:
			fprintf(stderr, "bast: Internal error: Bad output type %u\n", outtype);
			return(EXIT_FAILURE);
		break;
	}
	/* END: CREATE OUTPUT */
	
	if(emu && outtype==TAPE)
	{
		char *emucmd=getenv("EMU");
		if(emucmd)
		{
			char *cmd;
			int l,i;
			init_char(&cmd, &l, &i);
			while(*emucmd)
			{
				if(*emucmd=='%')
				{
					append_str(&cmd, &l, &i, outfile);
					emucmd++;
				}
				else
				{
					append_char(&cmd, &l, &i, *emucmd++);
				}
			}
			system(cmd);
			free(cmd);
		}
	}
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
		if((c==EOF)||(c=='\n')||(c=='\r'))
			break;
		if(c!=0)
		{
			if(c=='\\')
			{
				signed int d=fgetc(fp);
				if((d==EOF)||(d=='\n')||(d=='\r'))
				{
					append_char(&lout, &l, &i, c);
					break;
				}
				else if(d=='\\')
				{
					append_char(&lout, &l, &i, d);
				}
				else if(!isxdigit(d))
				{
					append_char(&lout, &l, &i, c);
					append_char(&lout, &l, &i, d);
				}
				else
				{
					signed int e=fgetc(fp);
					if((e==EOF)||(e=='\n')||(e=='\r'))
					{
						append_char(&lout, &l, &i, c);
						append_char(&lout, &l, &i, d);
						break;
					}
					else if(!isxdigit(e))
					{
						append_char(&lout, &l, &i, c);
						append_char(&lout, &l, &i, d);
						append_char(&lout, &l, &i, e);
					}
					else
					{
						char x[3]={d, e, 0};
						unsigned int h;
						sscanf(x, "%02x", &h);
						if(h)
							append_char(&lout, &l, &i, h&0xFF);
						else
							append_str(&lout, &l, &i, "\\0");
					}
				}
			}
			else
			{
				append_char(&lout, &l, &i, c);
			}
		}
	}
	return(lout);
}

void init_char(char **buf, int *l, int *i)
{
	*l=80;
	*buf=(char *)malloc(*l);
	(*buf)[0]=0;
	*i=0;
}

void append_char(char **buf, int *l, int *i, char c)
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
		(*i)++; // in the absence of a buffer, we're just counting bytes
	}
}

void append_str(char **buf, int *l, int *i, char *str)
{
	while(*str) // not the most tremendously efficient implementation, but conceptually simple at least
	{
		append_char(buf, l, i, *str++);
	}
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

void tokenise(basline *b, char **inbas, int fbas, int renum)
{
	if(b)
	{
		int fline=b->sline;
		if(b->tok) free(b->tok);
		b->tok=NULL;
		b->ntok=0;
		int bt;
		if(b->text && !strchr("#.\n", *b->text))
		{
			char *ptr=b->text;
			if(!renum)
			{
				while(isspace(*ptr))
					ptr++;
				char *p=strchr(ptr, ' ');
				if(p)
				{
					sscanf(ptr, "%d", &b->number);
					ptr=p+1;
				}
			}
			if(!(renum||b->number))
			{
				fprintf(stderr, "bast: Missing line-number\n\t"LOC"\n", LOCARG);
				err=true;
			}
			else
			{
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
						if(debug) fprintf(stderr, "gettoken(%s)", curtok);
						token dat=gettoken(curtok, &bt);
						if(debug) fprintf(stderr, "\t= %02X\n", dat.tok);
						if(dat.tok) // token is recognised?
						{
							if(Wsebasic&&((dat.tok<6)||(strchr("&\\~", dat.tok))))
							{
								fprintf(stderr, "bast: Tokeniser: Warning: Used SE BASIC token %02X\n\t"LOC"\n", dat.tok, LOCARG);
							}
							b->ntok++;
							b->tok=(token *)realloc(b->tok, b->ntok*sizeof(token));
							b->tok[b->ntok-1]=dat;
							ptr+=tl-bt;
							tl=0;
							if(curtok) free(curtok);
							curtok=NULL;
							if(dat.tok==0xEA) // REM token; eat the rest of the line (as token.data)
							{
								while(isspace(*ptr))
									ptr++;
								b->tok[b->ntok-1].data=strdup(ptr);
								b->tok[b->ntok-1].dl=0; // not an embedded-zeros style REM; those aren't allowed here
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
					if(debug) fprintf(stderr, "gettoken(%s\\n)", curtok);
					append_char(&curtok, &l, &i, '\n');
					token dat=gettoken(curtok, &bt);
					if(debug) fprintf(stderr, "\t= %02X\n", dat.tok);
					if(dat.tok) // token is recognised?
					{
						if(Wsebasic&&((dat.tok<6)||(strchr("&\\~", dat.tok))))
						{
							fprintf(stderr, "bast: Tokeniser: Warning: Used SE BASIC token %02X\n\t"LOC"\n", dat.tok, LOCARG);
						}
						b->ntok++;
						b->tok=(token *)realloc(b->tok, b->ntok*sizeof(token));
						b->tok[b->ntok-1]=dat;
						ptr+=tl-bt;
						tl=0;
						if(curtok) free(curtok);
						curtok=NULL;
						if(dat.tok==0xEA) // REM token; eat the rest of the line (as token.data)
						{
							while(isspace(*ptr))
								ptr++;
							b->tok[b->ntok-1].data=strdup(ptr);
							b->tok[b->ntok-1].dl=0; // not an embedded-zeros style REM; those aren't allowed here
							ptr+=strlen(ptr);
						}
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
}

token gettoken(char *data, int *bt)
{
	token rv;
	rv.text=data;
	rv.tok=0;
	rv.data=NULL;
	*bt=0;
	if(*data<' ') // nonprinting characters (control chars, eg. colour codes)
	{
		rv.tok=TOKEN_NONPRINT;
		rv.data=(char *)malloc(2);
		rv.data[0]=*data;
		rv.data[1]=0;
		*bt=strlen(data+1);
		return(rv);
	}
	if(*data=='\\') // nul character, input as '\00' (can't use within strings or REM)
	{
		if(data[1]=='0')
		{
			rv.tok=TOKEN_NONPRINT;
			rv.data=(char *)malloc(1);
			rv.data[0]=0;
			*bt=strlen(data+2);
			return(rv);
		}
	}
	if(*data=='"')
	{
		char *sm=strchr(data+1, '"');
		if(sm && !sm[1])
		{
			rv.data=strdup(data+1);
			rv.data[sm-data-1]=0;
			rv.tok=TOKEN_STRING;
			*bt=0;
			return(rv);
		}
	}
	if((data[0]=='%') && isalpha(data[1]))
	{
		int sp=strspn(data+1, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
		if(sp && data[sp+1])
		{
			bool partof=false; // is a partial offset a possibility here?
			bool haveof=false; // do we have a full offset?
			if(strchr("+-", data[sp+1]))
			{
				if(data[sp+2])
				{
					if(isxdigit(data[sp+2]))
					{
						if(data[sp+3])
						{
							if(isxdigit(data[sp+3]))
							{
								unsigned int val;
								sscanf(data+sp+2, "%02x", &val);
								if(data[sp+1]=='+')
								{
									if(val<=0x7F)
									{
										haveof=true;
										rv.index=val;
									}
								}
								else if(data[sp+1]=='-')
								{
									if(val<=0x80)
									{
										haveof=true;
										rv.index=-val;
									}
								}
							}
						}
						else
						{
							partof=true;
						}
					}
				}
				else
				{
					partof=true;
				}
			}
			if(!partof)
			{
				rv.data=strdup(data+1);
				rv.data[sp]=0;
				rv.tok=TOKEN_LABEL;
				if(!haveof)
					rv.index=0;
				*bt=strlen(data+sp+(haveof?4:1));
				return(rv);
			}
		}
	}
	if((data[0]=='@') && isalpha(data[1]))
	{
		int sp=strspn(data+1, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
		if(sp && data[sp+1])
		{
			bool partof=false; // is a partial offset a possibility here?
			bool haveof=false; // do we have a full offset?
			if(strchr("+-", data[sp+1]))
			{
				if(data[sp+2])
				{
					if(isxdigit(data[sp+2]))
					{
						if(data[sp+3])
						{
							if(isxdigit(data[sp+3]))
							{
								unsigned int val;
								sscanf(data+sp+2, "%02x", &val);
								if(data[sp+1]=='+')
								{
									if(val<=0x7F)
									{
										haveof=true;
										rv.index=val;
									}
								}
								else if(data[sp+1]=='-')
								{
									if(val<=0x80)
									{
										haveof=true;
										rv.index=-val;
									}
								}
							}
						}
						else
						{
							partof=true;
						}
					}
				}
				else
				{
					partof=true;
				}
			}
			if(!partof)
			{
				rv.data=strdup(data+1);
				rv.data[sp]=0;
				rv.tok=TOKEN_PTRLBL;
				if(!haveof)
					rv.index=0;
				*bt=strlen(data+sp+(haveof?4:1));
				return(rv);
			}
		}
	}
	// test for number
	char *endptr;
	double num=strtod(data, &endptr);
	if(*endptr && !strchr("0123456789.eE", *endptr) && (endptr!=data))
	{
		// 0x0E		ZX floating point number (full representation in token.data is (decimal), in token.data2 is (ZXfloat[5]))
		rv.tok=TOKEN_ZXFLOAT;
		if(Ocutnumbers)
		{
			rv.data=strdup(".");
		}
		else
		{
			rv.data=(char *)malloc(endptr-data+1);
			strncpy(rv.data, data, endptr-data);
			rv.data[endptr-data]=0;
		}
		rv.data2=(char *)malloc(5);
		zxfloat(rv.data2, num);
		*bt=strlen(endptr);
		return(rv);
	}
	if(strncasecmp(data, "!HEX", 4)==0)
	{
		char *p=data+4;
		while(isspace(*p))
			p++;
		char *q=p;
		while(isxdigit(*q))
			q++;
		if((*q)&&(q>p)&&(q-p<=4))
		{
			char c=*q;
			*q=0;
			unsigned int val;
			sscanf(p, "%x", &val);
			fprintf(stderr, "bast: !HEX converted %s to %u\n", p, val);
			*q=c;
			*bt=strlen(q);
			rv.tok=TOKEN_ZXFLOAT;
			if(Ocutnumbers)
			{
				rv.data=strdup(".");
			}
			else
			{
				rv.data=(char *)malloc(6);
				sprintf(rv.data, "%u", val);
			}
			rv.data2=(char *)malloc(5);
			zxfloat(rv.data2, val);
			return(rv);
		}
	}
	if(strncasecmp(data, "!OCT", 4)==0)
	{
		char *p=data+4;
		while(isspace(*p))
			p++;
		char *q=p;
		while(strchr("01234567", *q))
			q++;
		if((*q)&&(q>p)&&(q-p<=5))
		{
			char c=*q;
			*q=0;
			unsigned int val;
			sscanf(p, "%o", &val);
			fprintf(stderr, "bast: !OCT converted %s to %u\n", p, val);
			*q=c;
			*bt=strlen(q);
			rv.tok=TOKEN_ZXFLOAT;
			if(Ocutnumbers)
			{
				rv.data=strdup(".");
			}
			else
			{
				rv.data=(char *)malloc(6);
				sprintf(rv.data, "%u", val);
			}
			rv.data2=(char *)malloc(5);
			zxfloat(rv.data2, val);
			return(rv);
		}
	}
	int i,j=-1;
	for(i=0;i<ntokens;i++)
	{
		if(strncasecmp(data, tokentable[i].text, min(max(strlen(tokentable[i].text), strlen(data)-1), strlen(data)))==0)
		{
			if(j==-1)
				j=i;
			else
				break;
		}
	}
	if((i==ntokens) && (j!=-1))
	{
		if(strlen(data)>=strlen(tokentable[j].text))
		{
			rv.tok=tokentable[j].tok;
			*bt=strlen(data+strlen(tokentable[j].text));
			return(rv);
		}
	}
	if((strncasecmp(data, "!link", 5)==0) && (data[strlen(data)-1]=='\n'))
	{
		rv.tok=TOKEN_RLINK;
		char *p=data+5;
		while(isspace(*p)) p++;
		rv.data=strdup(p);
		rv.data[strlen(p)-1]=0;
		*bt=0;
		return(rv);
	}
	if(!isalpha(data[strlen(data)-1]) && !isspace(data[strlen(data)-1])) // "GO " may be the start of GO TO or GO SUB, "ON " may be the start of an SE BASIC ON ERR.  For safety's sake, we don't accept a variable name until we know it can't be anything else
	{
		// assume it's a variable
		int i=0,s=0;
		while(data[i])
		{
			if(!isalpha(data[i]))
			{
				if(data[i]=='$')
				{
					s=1;i++;
				}
				else
				{
					s=i?0:2;
				}
				break;
			}
			i++;
		}
		if((!s) || ((s==1) && (i==2)))
		{
			rv.tok=s?TOKEN_VARSTR:TOKEN_VAR;
			rv.data=strdup(data);
			rv.data[i]=0;
			*bt=strlen(data+i);
			return(rv);
		}
	}
	return(rv);
}

void zxfloat(char *buf, double value)
{
	if((fabs(value-floor(value+0.5))<=fabs(value)*1e-12) && (fabs(value)<65535.5))
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
		int ex=1+floor(log2(fabs(value)));
		unsigned long mantissa=floor(fabs(value)*exp2(32-ex)+0.5);
		buf[0]=ex+128;
		buf[1]=((mantissa>>24)&0x7F)|((value<0)?0x80:0);
		buf[2]=mantissa>>16;
		buf[3]=mantissa>>8;
		buf[4]=mantissa;
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

void buildbas(bas_seg *bas, bool write) // if write is false, we just compute offsets
{
	int dbl;
	if(write)
	{
		init_char(&bas->block, &dbl, &bas->blen);
	}
	else
	{
		bas->block=NULL;
	}
	int i;
	for(i=0;i<bas->nlines;i++) // Address of first line's number MSB is 0x5CCB.  Text starts 4 bytes later
	{
		bas->basic[i].offset=bas->blen+4+0x5CCB;
		if(bas->basic[i].ntok)
		{
			append_char(&bas->block, &dbl, &bas->blen, bas->basic[i].number>>8); // MSB first!!!!
			append_char(&bas->block, &dbl, &bas->blen, bas->basic[i].number);
			char *line;
			int li,ll;
			init_char(&line, &ll, &li);
			int j;
			for(j=0;j<bas->basic[i].ntok;j++)
			{
				if(bas->basic[i].tok[j].tok&0x80) // Keyword (or other high-bank token), pass thru untouched
				{
					append_char(&line, &ll, &li, bas->basic[i].tok[j].tok);
					if(bas->basic[i].tok[j].tok==0xEA) // REM token, rest-of-line in token.data
					{
						if(bas->basic[i].tok[j].dl) // has embedded \0s (is an object file)
						{
							int ri;
							for(ri=0;ri<bas->basic[i].tok[j].dl;ri++)
								append_char(&line, &ll, &li, bas->basic[i].tok[j].data[ri]);
						}
						else
						{
							char *p=bas->basic[i].tok[j].data;
							while(*p)
							{
								if((*p=='\\')&&(p[1]=='0')) // handle \\0 -> \0
								{
									append_char(&line, &ll, &li, 0);
									p+=2;
								}
								else
								{
									append_char(&line, &ll, &li, *p++);
								}
							}
						}
					}
				}
				else
				{
					int k;
					for(k=0;k<ntokens;k++)
					{
						char data[2]={bas->basic[i].tok[j].tok, 0};
						if(strcasecmp(data, tokentable[k].text)==0)
						{
							append_char(&line, &ll, &li, bas->basic[i].tok[j].tok);
							break;
						}
					}
					if(k==ntokens)
					{
						switch(bas->basic[i].tok[j].tok)
						{
							case TOKEN_VAR: // fallthrough
							case TOKEN_VARSTR:
								append_str(&line, &ll, &li, bas->basic[i].tok[j].data);
							break;
							case TOKEN_ZXFLOAT:
								append_str(&line, &ll, &li, bas->basic[i].tok[j].data);
								append_char(&line, &ll, &li, TOKEN_ZXFLOAT);
								int l;
								for(l=0;l<5;l++)
									append_char(&line, &ll, &li, bas->basic[i].tok[j].data2[l]);
							break;
							case TOKEN_STRING:
								append_char(&line, &ll, &li, '"');
								char *p=bas->basic[i].tok[j].data;
								while(*p)
								{
									if((*p=='\\')&&(p[1]=='0')) // handle \\0 -> \0
									{
										append_char(&line, &ll, &li, 0);
										p+=2;
									}
									else
									{
										append_char(&line, &ll, &li, *p++);
									}
								}
								append_char(&line, &ll, &li, '"');
							break;
							case TOKEN_RLINK:
								if(bas->basic[i].tok[j].data2)
								{
									if(write)
									{
										append_char(&line, &ll, &li, (signed char)0xEA);
										int l;
										for(l=0;l<((bin_seg *)bas->basic[i].tok[j].data2)->nbytes;l++)
											append_char(&line, &ll, &li, ((bin_seg *)bas->basic[i].tok[j].data2)->bytes[l].byte);
									}
									else
									{
										append_char(&line, &ll, &li, TOKEN_RLINK);
										int l;
										for(l=0;l<((bin_seg *)bas->basic[i].tok[j].data2)->nbytes;l++)
											append_char(&line, &ll, &li, 0);
										((bin_seg *)bas->basic[i].tok[j].data2)->org=bas->basic[i].offset+1;
									}
								}
							break;
							case TOKEN_NONPRINT:
								if(bas->basic[i].tok[j].data)
								{
									append_char(&line, &ll, &li, *bas->basic[i].tok[j].data);
								}
							break;
							case TOKEN_PTRLBL:
								if(bas->basic[i].tok[j].data)
								{
									if(write)
									{
										fprintf(stderr, "bast: buildbas: Internal error: TOKEN_PTRLBL in second pass\n");
										bas->blen=-1;
										goto exit;
									}
									else
									{
										append_char(&line, &ll, &li, TOKEN_PTRLBL);
										int l;
										for(l=0;l<(Ocutnumbers?6:10);l++)
											append_char(&line, &ll, &li, 0);
									}
								}
							break;
							case TOKEN_LABEL:
								if(bas->basic[i].tok[j].data)
								{
									if(write)
									{
										fprintf(stderr, "bast: buildbas: Internal error: TOKEN_LABEL in second pass\n");
										bas->blen=-1;
										goto exit;
									}
									else
									{
										append_char(&line, &ll, &li, TOKEN_LABEL);
										int l;
										for(l=0;l<(Ocutnumbers?6:10);l++)
											append_char(&line, &ll, &li, 0);
									}
								}
							break;
							default:
								fprintf(stderr, "bast: buildbas: Internal error: Bad token 0x%02X\n", bas->basic[i].tok[j].tok);
								bas->blen=-1;
								goto exit;
							break;
						}
					}
				}
			}
			append_char(&line, &ll, &li, 0x0D); // 0x0D is ENTER in ZX charset
			append_char(&bas->block, &dbl, &bas->blen, li);
			append_char(&bas->block, &dbl, &bas->blen, li>>8);
			for(j=0;j<li;j++)
				append_char(&bas->block, &dbl, &bas->blen, line[j]);
			free(line);
		}
	}
	exit:;
}

void bin_load(char *fname, FILE *fp, bin_seg * buf, char **name)
{
	if(buf)
	{
		buf->nbytes=0;
		buf->bytes=NULL;
		fprintf(stderr, "bast: Linker (object): reading %s\n", fname);
		char *line=fgetl(fp);
		int len=0;
		int i=1;
		while(line&&!feof(fp))
		{
			if(*line)
			{
				if(*line=='@')
				{
					sscanf(line, "@%04x", (unsigned int *)&buf->org);
				}
				else if(*line=='#')
				{
					if(name)
						*name=strdup(line+1);
				}
				else if(*line=='*')
				{
					sscanf(line, "*%04x", (unsigned int *)&len);
				}
				else
				{
					unsigned char cksum=0;
					char *ent=strtok(line, " \t");
					bin_byte row[8];
					int col;
					for(col=0;col<8;col++)
					{
						if(isxdigit(ent[0])&&isxdigit(ent[1]))
						{
							unsigned int n;
							sscanf(ent, "%02x", &n);
							row[col].type=BYTE;
							row[col].byte=n;
							cksum^=n;
						}
						else if((ent[0]=='$')&&(ent[1]=='$'))
						{
							if((!len)||(buf->nbytes+col+1>=len))
							{
								row[col].type=BNONE;
								row[col].byte=0;
							}
							else
							{
								fprintf(stderr, "bast: Linker (object): Bad pair %s (too soon)\n\t%s:%u\n", ent, fname, i);
								err=true;
							}
						}
						else
						{
							fprintf(stderr, "bast: Linker (object): Bad pair %s\n\t%s:%u\n", ent, fname, i);
							err=true;
						}
						ent=strtok(NULL, " \t");
					}
					if(ent&&(ent[0]=='=')&&(ent[1]=='='))
					{
						ent=strtok(NULL, "");
					}
					else
					{
						fprintf(stderr, "bast: Linker (object): Bad pair %s\n\t%s:%u\n", ent, fname, i);
						err=true;
					}
					if(isxdigit(ent[0])&&isxdigit(ent[1]))
					{
						unsigned int n;
						sscanf(ent, "%02x", &n);
						if(cksum!=n)
						{
							fprintf(stderr, "bast: Linker (object): Checksum failed: got %02x, expected %02x\n\t%s:%u\n", n, cksum, fname, i);
							err=true;
						}
						else
						{
							col=0;
							while((col<8) && row[col].type!=BNONE)
							{
								buf->nbytes++;
								buf->bytes=(bin_byte *)realloc(buf->bytes, buf->nbytes*sizeof(bin_byte));
								buf->bytes[buf->nbytes-1]=row[col++];
							}
						}
					}
				}
			}
			free(line);
			line=err?NULL:fgetl(fp);
			i++;
		}
		fclose(fp);
		if(len && (len!=buf->nbytes))
		{
			fprintf(stderr, "bast: Linker (object): %s got bad count %u bytes of %u\n", fname, buf->nbytes, len);
			err=true;
		}
		else if(!len && Wobjlen)
		{
			fprintf(stderr, "bast: Linker (object): Warning, no length directive in %s\n", fname);
		}
		else
		{
			fprintf(stderr, "bast: Linker (object): %s got %u bytes\n", fname, buf->nbytes);
		}
	}
	else
	{
		fprintf(stderr, "bast: Linker (object): Internal error: %s no buffer to write!\n", fname);
		err=true;
	}
}
