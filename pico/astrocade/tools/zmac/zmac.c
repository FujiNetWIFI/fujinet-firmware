/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 1 "zmac.y"

/*
 *  zmac -- macro cross-assembler for the Zilog Z80 microprocessor
 *
 *  Bruce Norskog	4/78
 *
 *  Last modification 2000-07-01 by mgr
 *
 *  This assembler is modeled after the Intel 8080 macro cross-assembler
 *  for the Intel 8080 by Ken Borgendale.  The major features are:
 *	1.  Full macro capabilities
 *	2.  Conditional assembly
 *	3.  A very flexible set of listing options and pseudo-ops
 *	4.  Symbol table output
 *	5.  Error report
 *	6.  Elimination of sequential searching
 *	7.  Commenting of source
 *	8.  Facilities for system definiton files
 *
 * (Revision history is now in ChangeLog. -rjm)
 */

#define ZMAC_VERSION	"1.3"
/* #define ZMAC_BETA	"b4" */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include "mio.h"
#include "getoptn.h"

#define UNUSED(var) ((void) var)

#if defined (__riscos__) && !defined (__riscos)
#define __riscos
#endif

#ifdef __riscos
#include "swis.h"
#define DDEUtils_Prefix		0x42580
#define DDEUtils_ThrowbackStart	0x42587
#define DDEUtils_ThrowbackSend	0x42588
#define DDEUtils_ThrowbackEnd	0x42589
#endif

#ifndef OS_DIR_SEP
#if defined (MSDOS)
#define OS_DIR_SEP '\\'
#elif defined (__riscos)
#define OS_DIR_SEP '.'
#else
#define OS_DIR_SEP '/'
#endif
#endif

#ifndef OS_EXT_SEP
#if defined (__riscos)
#define OS_EXT_SEP '/'
#else
#define OS_EXT_SEP '.'
#endif
#endif

/*
 * DEBUG turns on pass reporting.
 * Macro debug and Token debug enables.
#define	DEBUG
#define	M_DEBUG
#define	T_DEBUG
 */

#define ITEMTABLESIZE	2000
#define TEMPBUFSIZE	200
#define LINEBUFFERSIZE	200
#define EMITBUFFERSIZE	200
#define MAXSYMBOLSIZE	40
#define IFSTACKSIZE	20
#define MAXIFS		1024
#define TITLELEN	50
#define BINPERLINE	16
#define	PARMMAX		25
#define MAXEXP		25
#define SYMMAJIC	07203
#define	NEST_IN		8


#define loop	for(;;)

void yyerror(char *err)
{
	UNUSED (err);		/* we will do our own error printing */
	/* printf ("Oops! %s\n", err); */
}

struct	item	{
	char	*i_string;
	int	i_value;
	int	i_token;
	int	i_uses;
	int	i_equbad;
};

FILE	*fout,
	*fbuf,
	*fin[NEST_IN],
	*now_file ;

int	pass2;	/* set when pass one completed */
int	dollarsign ;	/* location counter */
int	olddollar ;	/* kept to put out binary */

/* program counter save for PHASE/DEPHASE */
int	phdollar, phbegin, phaseflag ;

char	*src_name[NEST_IN] ;
int	linein[NEST_IN] ;
int	now_in ;


#define bflag	0	/* balance error */
#define eflag	1	/* expression error */
#define fflag	2	/* syntax error */
#define iflag	3	/* bad digits */
#define mflag	4	/* multiply defined */
#define pflag	5	/* phase error */
#define uflag	6	/* undeclared used */
#define vflag	7	/* value out of range */
#define oflag	8	/* phase/dephase error */
#define frflag	9	/* double forward ref. via equ error */
#define zflag	10	/* Z80-only instruction (when `-z' option in use) */
#define orgflag 11	/* retrograde org error (when `-h' option not in use) */

#define FLAGS	12	/* number of flags */

char	err[FLAGS];
int	keeperr[FLAGS];
char	errlet[FLAGS]="BEFIMPUVORZG";
char	*errname[FLAGS]={
	"Balance",
	"Expression",
	"Syntax",
	"Digit",
	"Mult. def.",
	"Phase",
	"Undeclared",
	"Value",
	"Phase/Dephase",
	"Forward ref. to EQU with forward ref.",
	"Z80-specific instruction",
	"Retrograde ORG"
};
char	*warnname[]={
	"Symbol length exceeded",
	"Non-standard syntax",
	"Could replace JP with JR",
	"Could replace LD A, 0 with XOR A if flags unimportant",
	"Could replace RLC A with RLCA if S, Z and P/V flags unimportant",
	"Could replace RRC A with RRCA if S, Z and P/V flags unimportant",
	"Could replace RL A with RLA if S, Z and P/V flags unimportant",
	"Could replace RR A with RRA if S, Z and P/V flags unimportant",
	"Could replace SLA A with ADD A, A if H and P/V flags unimportant"
};

/* for "0 symbols", "1 symbol", "2 symbols", etc. */
#define DO_PLURAL(x)	(x),((x)==1)?"":"s"

char	linebuf[LINEBUFFERSIZE];
char	*lineptr;
char	*linemax = linebuf+LINEBUFFERSIZE;

char	outbin[BINPERLINE];
char	*outbinp = outbin;
char	*outbinm = outbin+BINPERLINE;

char	emitbuf[EMITBUFFERSIZE];
char	*emitptr;

char	ifstack[IFSTACKSIZE];
char	*ifptr;
char	*ifstmax = ifstack+IFSTACKSIZE-1;


char	expif[MAXIFS];
char	*expifp;
char	*expifmax = expif+MAXIFS;

char	hexadec[] = "0123456789ABCDEF" ;
char	*expstack[MAXEXP];
int	expptr;


int	nitems;
int	linecnt;
int	nbytes;
int	invented;


char	tempbuf[TEMPBUFSIZE];
char	*tempmax = tempbuf+TEMPBUFSIZE-1;

char	inmlex;
char	arg_flag;
char	quoteflag;
int	parm_number;
int	exp_number;
char	symlong[] = "Symbol too long";

int	disp;
#define FLOC	PARMMAX
#define TEMPNUM	PARMMAX+1
char	**est;
char	**est2;

char	*floc;
int	mfptr;
FILE	*mfile;


char	*title;
char	titlespace[TITLELEN];
char	*timp;
char	*sourcef;
/* changed to cope with filenames longer than 14 chars -rjm 1998-12-15 */
char	src[1024];
char	bin[1024];
char	mtmp[1024];
char	listf[1024];
char	writesyms[1024];
#ifdef __riscos
char	riscos_thbkf[1024];
#endif

char	bopt = 1,
	edef = 1,
	eopt = 1,
	fdef = 0,
	fopt = 0,
	gdef = 1,
	gopt = 1,
	iopt = 0 ,	/* list include files */
	lstoff = 0,
	lston = 0,	/* flag to force listing on */
	lopt = 0,
	mdef = 0,
	mopt = 0,
	nopt = 1,	/* line numbers on as default */
	oldoopt = 0,
	popt = 1,	/* form feed as default page eject */
	sopt = 0,	/* turn on symbol table listing */
	output_hex = 0,	/* `-h', output .hex rather than .bin -rjm */
	output_8080_only = 0,	/* `-z', output 8080-compat. ops only -rjm */
	show_error_line = 0,	/* `-S', show line which caused error -rjm */
	terse_lst_errors = 0,	/* `-t', terse errors in listing -rjm */
	continuous_listing = 1,	/* `-d', discontinuous - with page breaks */
	suggest_optimise = 0,	/* `-O', suggest optimisations -mgr */
#ifdef __riscos
	riscos_thbk = 0,	/* `-T', RISC OS throwback -mep */
#endif
	output_amsdos = 0,	/* `-A', AMSDOS binary file output -mep */
	saveopt;

char	xeq_flag = 0;
int	xeq;

time_t	now;
int	line;
int	page = 1;

int	had_errors = 0;		/* if program had errors, do exit(1) */
#ifdef __riscos
int	riscos_throwback_started = 0;
#endif
int	not_seen_org = 1;
int	first_org_store = 0;

struct stab {
	char	t_name[MAXSYMBOLSIZE+1];
	int	t_value;
	int	t_token;
};

/*
 *  push back character
 */
int	peekc;


/* function prototypes */
int addtoline(int ac);
int iflist(void);
int yylex(void);
int tokenofitem(int deftoken);
int nextchar(void);
int skipline(int ac);
void usage(void);
int main(int argc, char *argv[]);
int getarg(void);
int getm(void);
void yyerror(char *err);
void emit(int num, ...);
void emit1(int opcode,int regvalh,int data16,int type);
void emitdad(int rp1,int rp2);
void emitjr(int opcode,int expr);
void emitjp(int opcode,int expr);
void putbin(int v);
void flushbin(void);
void puthex(char byte, FILE *buf);
void list(int optarg);
void lineout(void);
void eject(void);
void space(int n);
void lsterr1(void);
void lsterr2(int lst);
void errorprt(int errnum);
void warnprt(int warnnum, int warnoff);
void list1(void);
void interchange(int i, int j);
void custom_qsort(int m, int n);
void setvars(void);
void error(char *as);
void fileerror(char *as,char *filename);
void justerror(char *as);
void putsymtab(void);
void erreport(void);
void mlex(void);
void suffix_if_none(char *str,char *suff);
void suffix(char *str,char *suff);
void decanonicalise(char *str);
void putm(char c);
void popsi(void);
char *getlocal(int c, int n);
void insymtab(char *name);
void outsymtab(char *name);
void copyname(char *st1, char *st2);
void next_source(char *sp);
void doatexit (void);
#ifdef __riscos
void riscos_set_csd(char *sp);
void riscos_throwback(int severity, char *file, int line, char *error);
#endif




/*
 *  add a character to the output line buffer
 */
int addtoline(int ac)
{
	/* check for EOF from stdio */
	if (ac == -1)
		ac = 0 ;
	if (inmlex)
		return(ac);
	if (lineptr >= linemax)
		error("line buffer overflow");
	*lineptr++ = ac;
	return(ac);
}


/*
 *  put values in buffer for outputing
 */

void emit(int bytes, ...)
{
	va_list ap;
	unsigned char *oldemitptr=(unsigned char *)emitptr;
	int c;

	va_start(ap,bytes);

	while	(--bytes >= 0)
		if (emitptr >= &emitbuf[EMITBUFFERSIZE])
			error("emit buffer overflow");
		else {
			*emitptr++ = va_arg(ap,int);
		}

	if (output_8080_only) {
		/* test for Z80-specific ops. These start with one of
		 * sixteen byte values, listed below. The values were
		 * taken from "A Z80 Workshop Manual" by E. A. Parr. -rjm
		 */
		/* As far as I can tell from my own literature
		 * review, 0x02, 0x0a, 0x12 and 0x1a are valid
		 * 8080 opcodes (LDAX/STAX B/D) -mgr
		 */
		c=*oldemitptr;
		if (/* c==0x02 || */ c==0x08 || /* c==0x0a || */ c==0x10 ||
		    /* c==0x12 || */ c==0x18 || /* c==0x1a || */ c==0x20 ||
		    c==0x28 || c==0x30 || c==0x38 || c==0xcb ||
		    c==0xd9 || c==0xdd || c==0xed || c==0xfd)
			err[zflag]++;
	}

	va_end(ap);
}

/* for emitted data - as above, without 8080 test.
 * Duplicating the code was easier than putting an extra arg in all
 * those emit()s. :-} Hopefully this isn't too unbearably nasty. -rjm
 */
void dataemit(int bytes, ...)
{
	va_list ap;

	va_start(ap,bytes);

	while	(--bytes >= 0)
		if (emitptr >= &emitbuf[EMITBUFFERSIZE])
			error("emit buffer overflow");
		else {
			*emitptr++ = va_arg(ap,int);
		}
	va_end(ap);
}


void emit1(int opcode,int regvalh,int data16,int type)
{
	if ((regvalh & 0x8000)) {	/* extra brackets to silence -Wall */
		if ((type & 1) == 0 && (disp > 127 || disp < -128))
			err[vflag]++;
		switch(type) {
		case 0:
			if (opcode & 0x8000)
				emit(4, regvalh >> 8, opcode >> 8, disp, opcode);
			else
				emit(3, regvalh >> 8, opcode, disp);
			break;
		case 1:
			emit(2, regvalh >> 8, opcode);
			break;
		case 2:
			if (data16 > 255 || data16 < -128)
				err[vflag]++;
			emit(4, regvalh >> 8, opcode, disp, data16);
			break;
		case 5:
			emit(4, regvalh >> 8, opcode, data16, data16 >> 8);
		}
	} else
		switch(type) {
		case 0:
			if (opcode & 0100000)
				emit(2, opcode >> 8, opcode);
			else
				emit(1, opcode);
			break;
		case 1:
			if (opcode & 0100000)
				emit(2, opcode >> 8, opcode);
			else
				emit(1, opcode);
			break;
		case 2:
			if (data16 > 255 || data16 < -128)
				err[vflag]++;
			emit(2, opcode, data16);
			break;
		case 3:
			if (data16 >255 || data16 < -128)
				err[vflag]++;
			emit(2, opcode, data16);
			break;
		case 5:
			if (opcode & 0100000)
				emit(4, opcode >> 8, opcode, data16, data16 >> 8);
			else
				emit(3, opcode, data16, data16 >> 8);
		}
}




void emitdad(int rp1,int rp2)
{
	if (rp1 & 0x8000)
		emit(2,rp1 >> 8, rp2 + 9);
	else
		emit(1,rp2 + 9);
}


void emitjr(int opcode,int expr)
{
	disp = expr - dollarsign - 2;
	if (disp > 127 || disp < -128)
		err[vflag]++;
	emit(2, opcode, disp);
}



void emitjp(int opcode,int expr)
{
	if (suggest_optimise && pass2 && opcode <= 0xda && !output_8080_only) {
		disp = expr - dollarsign - 2;
		if (disp <= 127 && disp >= -128)
			warnprt (2, 0);
	}
	emit(3, opcode, expr, expr >> 8);
}




/*
 *  put out a byte of binary
 */
void putbin(int v)
{
	if(!pass2 || !bopt) return;
	*outbinp++ = v;
	if (outbinp >= outbinm) flushbin();
}



/*
 *  output one line of binary in INTEL standard form
 */
void flushbin()
{
	char *p;
	int check=outbinp-outbin;

	if (!pass2 || !bopt)
		return;
	nbytes += check;
	if (check) {
		if (output_hex) {
			putc(':', fbuf);
			puthex(check, fbuf);
			puthex(olddollar>>8, fbuf);
			puthex(olddollar, fbuf);
			puthex(0, fbuf);
		}
		check += (olddollar >> 8) + olddollar;
		olddollar += (outbinp-outbin);
		for (p=outbin; p<outbinp; p++) {
			if (output_hex)
				puthex(*p, fbuf);
			else
				fputc(*p, fbuf);
			check += *p;
		}
		if (output_hex) {
			puthex(256-check, fbuf);
			putc('\n', fbuf);
		}
		outbinp = outbin;
	}
}



/*
 *  put out one byte of hex
 */
void puthex(char byte, FILE *buf)
{
	putc(hexadec[(byte >> 4) & 017], buf);
	putc(hexadec[byte & 017], buf);
}

/*
 *  put out a line of output -- also put out binary
 */
void list(int optarg)
{
	char *	p;
	int	i;
	int  lst;

	if (!expptr)
		linecnt++;
	addtoline('\0');
	if (pass2) {
		lst = iflist();
		if (lst) {
			lineout();
			if (nopt)
				fprintf(fout, "%4d:\t", linein[now_in]);
			puthex(optarg >> 8, fout);
			puthex(optarg, fout);
			fputs("  ", fout);
			for (p = emitbuf; (p < emitptr) && (p - emitbuf < 4); p++) {
				puthex(*p, fout);
			}
			for (i = 4 - (p-emitbuf); i > 0; i--)
				fputs("  ", fout);
			putc('\t', fout);
			fputs(linebuf, fout);
		}

		if (bopt) {
			for (p = emitbuf; p < emitptr; p++)
				putbin(*p);
		}


		p = emitbuf+4;
		while (lst && gopt && p < emitptr) {
			lineout();
			if (nopt) putc('\t', fout);
			fputs("      ", fout);
			for (i = 0; (i < 4) && (p < emitptr);i++) {
				puthex(*p, fout);
				p++;
			}
			putc('\n', fout);
		}


		lsterr2(lst);
	} else
		lsterr1();
	dollarsign += emitptr - emitbuf;
	emitptr = emitbuf;
	lineptr = linebuf;
}



/*
 *  keep track of line numbers and put out headers as necessary
 */
void lineout()
{
	if (continuous_listing) {
		line = 1;
		return;
	}
	if (line == 60) {
		if (popt)
			putc('\014', fout);	/* send the form feed */
		else
			fputs("\n\n\n\n\n", fout);
		line = 0;
	}
	if (line == 0) {
		fprintf(fout, "\n\n%s %s\t%s\t Page %d\n\n\n",
			&timp[4], &timp[20], title, page++);
		line = 4;
	}
	line++;
}


/*
 *  cause a page eject
 */
void eject()
{
	if (pass2 && !continuous_listing && iflist()) {
		if (popt) {
			putc('\014', fout);	/* send the form feed */
		} else {
			while (line < 65) {
				line++;
				putc('\n', fout);
			}
		}
	}
	line = 0;
}


/*
 *  space n lines on the list file
 */
void space(int n)
{
	int	i ;
	if (pass2 && iflist())
		for (i = 0; i<n; i++) {
			lineout();
			putc('\n', fout);
		}
}


/*
 *  Error handling - pass 1
 */
void lsterr1()
{
	int i;
	for (i = 0; i <= 4; i++)
		if (err[i]) {
			errorprt(i);
			err[i] = 0;
		}
}


/*
 *  Error handling - pass 2.
 */
void lsterr2(int lst)
{
	int i;
	for (i=0; i<FLAGS; i++)
		if (err[i]) {
			if (lst) {
				lineout();
				/* verbose inline error messages now,
				 * must override with `-t' to get old
				 * behaviour. -rjm
				 */
				if (terse_lst_errors)
					putc(errlet[i], fout);
				else
					fprintf(fout,"*** %s error ***",
							errname[i]);
				putc('\n', fout);
			}
			err[i] = 0;
			keeperr[i]++;
			if (i > 4)
				errorprt(i);
		}

	fflush(fout);	/* to avoid putc(har) mix bug */
}

/*
 *  print diagnostic to error terminal
 */
void errorprt(int errnum)
{
	had_errors=1;
	fprintf(stderr,"%s:%d: %s error\n",
		src_name[now_in], linein[now_in], errname[errnum]);
	if(show_error_line)
		fprintf(stderr, "%s\n", linebuf);
#ifdef __riscos
	if (riscos_thbk)
		riscos_throwback (1, src_name[now_in], linein[now_in], errname[errnum]);
#endif
}


/*
 *  print warning to error terminal
 */
void warnprt(int warnnum, int warnoff)
{
	fprintf(stderr,"%s:%d: warning: %s\n",
		src_name[now_in], linein[now_in] + warnoff, warnname[warnnum]);
		/* Offset needed if warning issued while line is being parsed */
#ifdef __riscos
	if (riscos_thbk)
		riscos_throwback (0, src_name[now_in], linein[now_in] + warnoff, warnname[warnnum]);
#endif
	/* if(show_error_line)
		Can't show line because it isn't necessarily complete
		fprintf(stderr, "%s\n", linebuf); */
}


/*
 *  list without address -- for comments and if skipped lines
 */
void list1()
{
	int lst;

	addtoline('\0');
	lineptr = linebuf;
	if (!expptr) linecnt++;
	if (pass2)
	{
		if ((lst = iflist())) {
			lineout();
			if (nopt)
				fprintf(fout, "%4d:\t", linein[now_in]);
			fprintf(fout, "\t\t%s", linebuf);
			lsterr2(lst);
		}
	} else
		lsterr1();
}


/*
 *  see if listing is desired
 */
int iflist()
{
	int i, j;

	if (lston)
		return(1) ;
	if (lopt)
		return(0);
	if (*ifptr && !fopt)
		return(0);
	if (!lstoff && !expptr)
		return(1);
	j = 0;
	for (i=0; i<FLAGS; i++)
		if (err[i])
			j++;
	if (expptr)
		return(mopt || j);
	if (eopt && j)
		return(1);
	return(0);
}


/* moved out of %{..%} bit in parse routine because `bison -y'
 * didn't like it... -rjm
 */
char  *cp;

int list_tmp1,list_tmp2;
int equ_bad_label=0;


#line 899 "y.tab.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif


/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    STRING = 258,                  /* STRING  */
    NOOPERAND = 259,               /* NOOPERAND  */
    ARITHC = 260,                  /* ARITHC  */
    ADD = 261,                     /* ADD  */
    LOGICAL = 262,                 /* LOGICAL  */
    AND = 263,                     /* AND  */
    OR = 264,                      /* OR  */
    XOR = 265,                     /* XOR  */
    BIT = 266,                     /* BIT  */
    CALL = 267,                    /* CALL  */
    INCDEC = 268,                  /* INCDEC  */
    DJNZ = 269,                    /* DJNZ  */
    EX = 270,                      /* EX  */
    IM = 271,                      /* IM  */
    PHASE = 272,                   /* PHASE  */
    DEPHASE = 273,                 /* DEPHASE  */
    IN = 274,                      /* IN  */
    JP = 275,                      /* JP  */
    JR = 276,                      /* JR  */
    LD = 277,                      /* LD  */
    OUT = 278,                     /* OUT  */
    PUSHPOP = 279,                 /* PUSHPOP  */
    RET = 280,                     /* RET  */
    SHIFT = 281,                   /* SHIFT  */
    RST = 282,                     /* RST  */
    REGNAME = 283,                 /* REGNAME  */
    ACC = 284,                     /* ACC  */
    C = 285,                       /* C  */
    RP = 286,                      /* RP  */
    HL = 287,                      /* HL  */
    INDEX = 288,                   /* INDEX  */
    AF = 289,                      /* AF  */
    SP = 290,                      /* SP  */
    MISCREG = 291,                 /* MISCREG  */
    F = 292,                       /* F  */
    COND = 293,                    /* COND  */
    SPCOND = 294,                  /* SPCOND  */
    NUMBER = 295,                  /* NUMBER  */
    UNDECLARED = 296,              /* UNDECLARED  */
    END = 297,                     /* END  */
    ORG = 298,                     /* ORG  */
    DEFB = 299,                    /* DEFB  */
    DEFS = 300,                    /* DEFS  */
    DEFW = 301,                    /* DEFW  */
    EQU = 302,                     /* EQU  */
    DEFL = 303,                    /* DEFL  */
    LABEL = 304,                   /* LABEL  */
    EQUATED = 305,                 /* EQUATED  */
    WASEQUATED = 306,              /* WASEQUATED  */
    DEFLED = 307,                  /* DEFLED  */
    MULTDEF = 308,                 /* MULTDEF  */
    MOD = 309,                     /* MOD  */
    SHL = 310,                     /* SHL  */
    SHR = 311,                     /* SHR  */
    NOT = 312,                     /* NOT  */
    LT = 313,                      /* LT  */
    GT = 314,                      /* GT  */
    EQ = 315,                      /* EQ  */
    LE = 316,                      /* LE  */
    GE = 317,                      /* GE  */
    NE = 318,                      /* NE  */
    IF = 319,                      /* IF  */
    ELSE = 320,                    /* ELSE  */
    ENDIF = 321,                   /* ENDIF  */
    ARGPSEUDO = 322,               /* ARGPSEUDO  */
    LIST = 323,                    /* LIST  */
    MINMAX = 324,                  /* MINMAX  */
    MACRO = 325,                   /* MACRO  */
    MNAME = 326,                   /* MNAME  */
    OLDMNAME = 327,                /* OLDMNAME  */
    ARG = 328,                     /* ARG  */
    ENDM = 329,                    /* ENDM  */
    MPARM = 330,                   /* MPARM  */
    ONECHAR = 331,                 /* ONECHAR  */
    TWOCHAR = 332,                 /* TWOCHAR  */
    UNARY = 333                    /* UNARY  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif
/* Token kinds.  */
#define YYEMPTY -2
#define YYEOF 0
#define YYerror 256
#define YYUNDEF 257
#define STRING 258
#define NOOPERAND 259
#define ARITHC 260
#define ADD 261
#define LOGICAL 262
#define AND 263
#define OR 264
#define XOR 265
#define BIT 266
#define CALL 267
#define INCDEC 268
#define DJNZ 269
#define EX 270
#define IM 271
#define PHASE 272
#define DEPHASE 273
#define IN 274
#define JP 275
#define JR 276
#define LD 277
#define OUT 278
#define PUSHPOP 279
#define RET 280
#define SHIFT 281
#define RST 282
#define REGNAME 283
#define ACC 284
#define C 285
#define RP 286
#define HL 287
#define INDEX 288
#define AF 289
#define SP 290
#define MISCREG 291
#define F 292
#define COND 293
#define SPCOND 294
#define NUMBER 295
#define UNDECLARED 296
#define END 297
#define ORG 298
#define DEFB 299
#define DEFS 300
#define DEFW 301
#define EQU 302
#define DEFL 303
#define LABEL 304
#define EQUATED 305
#define WASEQUATED 306
#define DEFLED 307
#define MULTDEF 308
#define MOD 309
#define SHL 310
#define SHR 311
#define NOT 312
#define LT 313
#define GT 314
#define EQ 315
#define LE 316
#define GE 317
#define NE 318
#define IF 319
#define ELSE 320
#define ENDIF 321
#define ARGPSEUDO 322
#define LIST 323
#define MINMAX 324
#define MACRO 325
#define MNAME 326
#define OLDMNAME 327
#define ARG 328
#define ENDM 329
#define MPARM 330
#define ONECHAR 331
#define TWOCHAR 332
#define UNARY 333

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 829 "zmac.y"

	struct item *itemptr;
	int ival;
	char *cval;
	

#line 1112 "y.tab.c"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);



/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_STRING = 3,                     /* STRING  */
  YYSYMBOL_NOOPERAND = 4,                  /* NOOPERAND  */
  YYSYMBOL_ARITHC = 5,                     /* ARITHC  */
  YYSYMBOL_ADD = 6,                        /* ADD  */
  YYSYMBOL_LOGICAL = 7,                    /* LOGICAL  */
  YYSYMBOL_AND = 8,                        /* AND  */
  YYSYMBOL_OR = 9,                         /* OR  */
  YYSYMBOL_XOR = 10,                       /* XOR  */
  YYSYMBOL_BIT = 11,                       /* BIT  */
  YYSYMBOL_CALL = 12,                      /* CALL  */
  YYSYMBOL_INCDEC = 13,                    /* INCDEC  */
  YYSYMBOL_DJNZ = 14,                      /* DJNZ  */
  YYSYMBOL_EX = 15,                        /* EX  */
  YYSYMBOL_IM = 16,                        /* IM  */
  YYSYMBOL_PHASE = 17,                     /* PHASE  */
  YYSYMBOL_DEPHASE = 18,                   /* DEPHASE  */
  YYSYMBOL_IN = 19,                        /* IN  */
  YYSYMBOL_JP = 20,                        /* JP  */
  YYSYMBOL_JR = 21,                        /* JR  */
  YYSYMBOL_LD = 22,                        /* LD  */
  YYSYMBOL_OUT = 23,                       /* OUT  */
  YYSYMBOL_PUSHPOP = 24,                   /* PUSHPOP  */
  YYSYMBOL_RET = 25,                       /* RET  */
  YYSYMBOL_SHIFT = 26,                     /* SHIFT  */
  YYSYMBOL_RST = 27,                       /* RST  */
  YYSYMBOL_REGNAME = 28,                   /* REGNAME  */
  YYSYMBOL_ACC = 29,                       /* ACC  */
  YYSYMBOL_C = 30,                         /* C  */
  YYSYMBOL_RP = 31,                        /* RP  */
  YYSYMBOL_HL = 32,                        /* HL  */
  YYSYMBOL_INDEX = 33,                     /* INDEX  */
  YYSYMBOL_AF = 34,                        /* AF  */
  YYSYMBOL_SP = 35,                        /* SP  */
  YYSYMBOL_MISCREG = 36,                   /* MISCREG  */
  YYSYMBOL_F = 37,                         /* F  */
  YYSYMBOL_COND = 38,                      /* COND  */
  YYSYMBOL_SPCOND = 39,                    /* SPCOND  */
  YYSYMBOL_NUMBER = 40,                    /* NUMBER  */
  YYSYMBOL_UNDECLARED = 41,                /* UNDECLARED  */
  YYSYMBOL_END = 42,                       /* END  */
  YYSYMBOL_ORG = 43,                       /* ORG  */
  YYSYMBOL_DEFB = 44,                      /* DEFB  */
  YYSYMBOL_DEFS = 45,                      /* DEFS  */
  YYSYMBOL_DEFW = 46,                      /* DEFW  */
  YYSYMBOL_EQU = 47,                       /* EQU  */
  YYSYMBOL_DEFL = 48,                      /* DEFL  */
  YYSYMBOL_LABEL = 49,                     /* LABEL  */
  YYSYMBOL_EQUATED = 50,                   /* EQUATED  */
  YYSYMBOL_WASEQUATED = 51,                /* WASEQUATED  */
  YYSYMBOL_DEFLED = 52,                    /* DEFLED  */
  YYSYMBOL_MULTDEF = 53,                   /* MULTDEF  */
  YYSYMBOL_MOD = 54,                       /* MOD  */
  YYSYMBOL_SHL = 55,                       /* SHL  */
  YYSYMBOL_SHR = 56,                       /* SHR  */
  YYSYMBOL_NOT = 57,                       /* NOT  */
  YYSYMBOL_LT = 58,                        /* LT  */
  YYSYMBOL_GT = 59,                        /* GT  */
  YYSYMBOL_EQ = 60,                        /* EQ  */
  YYSYMBOL_LE = 61,                        /* LE  */
  YYSYMBOL_GE = 62,                        /* GE  */
  YYSYMBOL_NE = 63,                        /* NE  */
  YYSYMBOL_IF = 64,                        /* IF  */
  YYSYMBOL_ELSE = 65,                      /* ELSE  */
  YYSYMBOL_ENDIF = 66,                     /* ENDIF  */
  YYSYMBOL_ARGPSEUDO = 67,                 /* ARGPSEUDO  */
  YYSYMBOL_LIST = 68,                      /* LIST  */
  YYSYMBOL_MINMAX = 69,                    /* MINMAX  */
  YYSYMBOL_MACRO = 70,                     /* MACRO  */
  YYSYMBOL_MNAME = 71,                     /* MNAME  */
  YYSYMBOL_OLDMNAME = 72,                  /* OLDMNAME  */
  YYSYMBOL_ARG = 73,                       /* ARG  */
  YYSYMBOL_ENDM = 74,                      /* ENDM  */
  YYSYMBOL_MPARM = 75,                     /* MPARM  */
  YYSYMBOL_ONECHAR = 76,                   /* ONECHAR  */
  YYSYMBOL_TWOCHAR = 77,                   /* TWOCHAR  */
  YYSYMBOL_78_ = 78,                       /* '|'  */
  YYSYMBOL_79_ = 79,                       /* '^'  */
  YYSYMBOL_80_ = 80,                       /* '&'  */
  YYSYMBOL_81_ = 81,                       /* '='  */
  YYSYMBOL_82_ = 82,                       /* '<'  */
  YYSYMBOL_83_ = 83,                       /* '>'  */
  YYSYMBOL_84_ = 84,                       /* '+'  */
  YYSYMBOL_85_ = 85,                       /* '-'  */
  YYSYMBOL_86_ = 86,                       /* '*'  */
  YYSYMBOL_87_ = 87,                       /* '/'  */
  YYSYMBOL_88_ = 88,                       /* '%'  */
  YYSYMBOL_89_ = 89,                       /* '!'  */
  YYSYMBOL_90_ = 90,                       /* '~'  */
  YYSYMBOL_UNARY = 91,                     /* UNARY  */
  YYSYMBOL_92_n_ = 92,                     /* '\n'  */
  YYSYMBOL_93_ = 93,                       /* ','  */
  YYSYMBOL_94_ = 94,                       /* ':'  */
  YYSYMBOL_95_ = 95,                       /* '.'  */
  YYSYMBOL_96_ = 96,                       /* '('  */
  YYSYMBOL_97_ = 97,                       /* ')'  */
  YYSYMBOL_98_ = 98,                       /* '\''  */
  YYSYMBOL_99_ = 99,                       /* '$'  */
  YYSYMBOL_100_ = 100,                     /* '['  */
  YYSYMBOL_101_ = 101,                     /* ']'  */
  YYSYMBOL_YYACCEPT = 102,                 /* $accept  */
  YYSYMBOL_statements = 103,               /* statements  */
  YYSYMBOL_statement = 104,                /* statement  */
  YYSYMBOL_colonordot = 105,               /* colonordot  */
  YYSYMBOL_maybecolon = 106,               /* maybecolon  */
  YYSYMBOL_107_label_part = 107,           /* label.part  */
  YYSYMBOL_operation = 108,                /* operation  */
  YYSYMBOL_109_parm_list = 109,            /* parm.list  */
  YYSYMBOL_110_parm_element = 110,         /* parm.element  */
  YYSYMBOL_111_arg_list = 111,             /* arg.list  */
  YYSYMBOL_112_arg_element = 112,          /* arg.element  */
  YYSYMBOL_reg = 113,                      /* reg  */
  YYSYMBOL_realreg = 114,                  /* realreg  */
  YYSYMBOL_mem = 115,                      /* mem  */
  YYSYMBOL_evenreg = 116,                  /* evenreg  */
  YYSYMBOL_pushable = 117,                 /* pushable  */
  YYSYMBOL_bcdesp = 118,                   /* bcdesp  */
  YYSYMBOL_bcdehlsp = 119,                 /* bcdehlsp  */
  YYSYMBOL_mar = 120,                      /* mar  */
  YYSYMBOL_condition = 121,                /* condition  */
  YYSYMBOL_spcondition = 122,              /* spcondition  */
  YYSYMBOL_123_db_list = 123,              /* db.list  */
  YYSYMBOL_124_db_list_element = 124,      /* db.list.element  */
  YYSYMBOL_125_dw_list = 125,              /* dw.list  */
  YYSYMBOL_126_dw_list_element = 126,      /* dw.list.element  */
  YYSYMBOL_lxexpression = 127,             /* lxexpression  */
  YYSYMBOL_expression = 128,               /* expression  */
  YYSYMBOL_parenexpr = 129,                /* parenexpr  */
  YYSYMBOL_noparenexpr = 130,              /* noparenexpr  */
  YYSYMBOL_symbol = 131,                   /* symbol  */
  YYSYMBOL_al = 132,                       /* al  */
  YYSYMBOL_arg_on = 133,                   /* arg_on  */
  YYSYMBOL_arg_off = 134,                  /* arg_off  */
  YYSYMBOL_setqf = 135,                    /* setqf  */
  YYSYMBOL_clrqf = 136                     /* clrqf  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_int16 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if !defined yyoverflow

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* !defined yyoverflow */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  2
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   1986

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  102
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  35
/* YYNRULES -- Number of rules.  */
#define YYNRULES  194
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  351

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   333


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      92,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    89,     2,     2,    99,    88,    80,    98,
      96,    97,    86,    84,    93,    85,    95,    87,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    94,     2,
      82,    81,    83,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,   100,     2,   101,    79,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,    78,     2,    90,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    91
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   928,   928,   931,   936,   941,   945,   976,   989,  1004,
    1030,  1039,  1045,  1050,  1057,  1076,  1113,  1119,  1125,  1173,
    1185,  1201,  1218,  1245,  1247,  1250,  1253,  1258,  1260,  1283,
    1309,  1312,  1315,  1318,  1330,  1333,  1336,  1339,  1342,  1345,
    1348,  1351,  1354,  1357,  1360,  1363,  1366,  1369,  1372,  1375,
    1378,  1381,  1384,  1387,  1390,  1393,  1396,  1399,  1402,  1411,
    1414,  1421,  1424,  1433,  1436,  1439,  1446,  1449,  1452,  1455,
    1458,  1461,  1464,  1467,  1470,  1473,  1482,  1489,  1497,  1506,
    1509,  1512,  1521,  1524,  1527,  1535,  1543,  1553,  1563,  1566,
    1569,  1582,  1585,  1588,  1595,  1598,  1606,  1618,  1628,  1654,
    1656,  1658,  1662,  1664,  1666,  1671,  1681,  1684,  1686,  1691,
    1702,  1704,  1707,  1712,  1717,  1723,  1728,  1734,  1741,  1743,
    1746,  1751,  1756,  1759,  1764,  1770,  1772,  1778,  1783,  1789,
    1791,  1797,  1802,  1806,  1808,  1811,  1816,  1823,  1833,  1835,
    1840,  1849,  1851,  1855,  1857,  1861,  1866,  1869,  1871,  1873,
    1876,  1888,  1891,  1894,  1901,  1904,  1907,  1910,  1913,  1916,
    1919,  1922,  1925,  1928,  1931,  1934,  1937,  1940,  1943,  1946,
    1949,  1952,  1955,  1958,  1961,  1964,  1967,  1970,  1973,  1976,
    1979,  1982,  1985,  1988,  1993,  1995,  1997,  1999,  2001,  2003,
    2008,  2021,  2025,  2029,  2033
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if YYDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "STRING", "NOOPERAND",
  "ARITHC", "ADD", "LOGICAL", "AND", "OR", "XOR", "BIT", "CALL", "INCDEC",
  "DJNZ", "EX", "IM", "PHASE", "DEPHASE", "IN", "JP", "JR", "LD", "OUT",
  "PUSHPOP", "RET", "SHIFT", "RST", "REGNAME", "ACC", "C", "RP", "HL",
  "INDEX", "AF", "SP", "MISCREG", "F", "COND", "SPCOND", "NUMBER",
  "UNDECLARED", "END", "ORG", "DEFB", "DEFS", "DEFW", "EQU", "DEFL",
  "LABEL", "EQUATED", "WASEQUATED", "DEFLED", "MULTDEF", "MOD", "SHL",
  "SHR", "NOT", "LT", "GT", "EQ", "LE", "GE", "NE", "IF", "ELSE", "ENDIF",
  "ARGPSEUDO", "LIST", "MINMAX", "MACRO", "MNAME", "OLDMNAME", "ARG",
  "ENDM", "MPARM", "ONECHAR", "TWOCHAR", "'|'", "'^'", "'&'", "'='", "'<'",
  "'>'", "'+'", "'-'", "'*'", "'/'", "'%'", "'!'", "'~'", "UNARY", "'\\n'",
  "','", "':'", "'.'", "'('", "')'", "'\\''", "'$'", "'['", "']'",
  "$accept", "statements", "statement", "colonordot", "maybecolon",
  "label.part", "operation", "parm.list", "parm.element", "arg.list",
  "arg.element", "reg", "realreg", "mem", "evenreg", "pushable", "bcdesp",
  "bcdehlsp", "mar", "condition", "spcondition", "db.list",
  "db.list.element", "dw.list", "dw.list.element", "lxexpression",
  "expression", "parenexpr", "noparenexpr", "symbol", "al", "arg_on",
  "arg_off", "setqf", "clrqf", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-230)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-142)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -230,   321,  -230,  -230,   -47,  -230,  -230,  -230,  -230,  -230,
    1851,   -87,   -63,  -230,  1758,   -17,  -230,  -230,  -230,   114,
     492,   -37,    48,  -230,  -230,  -230,  -230,  -230,  -230,  -230,
    1851,  -230,  1851,  1851,  1851,  1851,  1851,  -230,  1851,   605,
    -230,  -230,  -230,  -230,   -40,  -230,   640,  -230,  -230,  -230,
    -230,  1060,  1030,  1142,  1194,  1224,  1276,  1851,  1400,   119,
    1851,   -22,  1851,  1851,  -230,   143,  1371,  1452,    50,   -12,
     145,     1,    -9,  1851,  1780,  1851,    85,  1851,  1851,  -230,
    -230,  -230,    29,  1851,  1851,  1851,  -230,  -230,  -230,   -77,
    -230,  -230,  -230,  -230,  -230,  -230,   342,   212,  1851,  1851,
    1851,  1851,  1851,  1851,  1851,  1851,  1851,  1851,  1851,  1851,
    1851,  1851,  1851,  1851,  1851,  1851,  1851,  1851,  1851,  1851,
    1851,  -230,  -230,    32,  -230,  -230,    39,  -230,    51,  1706,
    -230,  -230,  -230,   913,    52,  -230,  -230,  -230,    60,   913,
      66,  -230,   913,    67,  -230,   913,    75,  -230,   913,    89,
    -230,   913,   513,  -230,  -230,  -230,    90,  -230,   913,  -230,
    -230,  -230,     9,  -230,  -230,  -230,  -230,   913,    98,    99,
     158,   913,   913,   111,   112,  1728,  -230,   113,   913,   115,
     913,   116,  1562,   117,   118,   120,  1481,   121,  -230,  -230,
    -230,  -230,  -230,  -230,   913,  -230,   696,   913,  -230,  -230,
     123,  -230,   913,   731,   124,  -230,   913,   134,  -230,   787,
     822,   549,  -230,    48,  -230,  -230,   926,   969,   382,  -230,
     -50,   -50,   102,   102,  1898,   102,   102,  1898,   969,   382,
     926,  1898,   102,   102,   -26,   -26,  -230,  -230,  -230,   126,
    -230,  1306,    87,   122,  1833,  1306,   108,  1306,  1306,  1306,
    1306,    -9,  1851,   191,   190,   128,   130,   131,   132,  1851,
    1851,   199,   133,  1112,  1652,   166,   135,   202,  -230,    85,
    -230,  1851,  -230,   -29,  -230,  -230,  -230,  1851,  -230,  -230,
    -230,   913,  -230,  -230,  -230,  -230,  -230,   398,  -230,   913,
    -230,  -230,  -230,   913,  -230,   913,  -230,   913,  -230,   913,
    -230,   913,  -230,  -230,   141,   205,  1533,  -230,  -230,   913,
     913,  -230,   150,  -230,  1623,  -230,   913,   146,   153,  -230,
    -230,  -230,   154,   155,  -230,  -230,   156,  -230,  -230,  -230,
    -230,   134,   878,  -230,   152,    95,   151,   157,   224,   160,
      43,  -230,  -230,  -230,  -230,  -230,  -230,  -230,  -230,  -230,
    -230
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       2,     0,     1,    22,   184,   185,   187,   188,   189,   186,
       0,     0,     0,   191,     0,     0,    23,    24,     3,     0,
       0,    25,   102,   147,   153,   146,   149,   150,   151,   154,
       0,   148,     0,     0,     0,     0,     0,   152,     0,     0,
     143,   144,    10,    11,   192,    17,     0,    20,   184,    28,
      30,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    97,     0,     0,     0,     0,     0,
       0,    73,     0,     0,     0,     0,     0,     0,     0,   190,
     101,     4,     0,     0,     0,     0,    26,    29,   105,     0,
     103,   179,   182,   183,   181,   180,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     9,   192,     0,    18,   112,   113,   114,     0,     0,
      48,   110,   111,    36,   113,   127,   128,    46,     0,    34,
     113,    50,    38,   113,    51,    39,   113,    52,    40,   113,
      53,    41,     0,   132,   130,   131,     0,   129,    32,   113,
     123,   124,     0,    59,    63,   118,   119,    72,     0,     0,
       0,    95,    96,     0,     0,     0,    68,     0,    31,     0,
      70,     0,     0,     0,     0,     0,     0,     0,   120,   121,
      64,   122,    74,    58,    33,    12,     0,    98,   136,   135,
      99,   133,   137,     0,   100,   138,   140,   106,     5,     0,
       0,     0,    19,     0,   145,   178,   162,   164,   166,   160,
     167,   168,   172,   174,   173,   175,   176,   177,   163,   165,
     161,   170,   169,   171,   155,   156,   158,   157,   159,     0,
      16,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    13,     0,
      14,     0,   109,     0,   107,     6,     7,     0,   104,    15,
      49,    37,   126,   125,    60,   115,   117,     0,    47,    35,
      61,    62,    54,    42,    55,    43,    56,    44,    57,    45,
      65,    69,    87,   193,     0,     0,     0,    90,    67,    66,
      71,    82,     0,    81,     0,    75,     0,   143,   144,   142,
      86,    83,   143,   144,    80,    85,     0,    93,   134,   139,
      21,     0,     0,   116,     0,     0,     0,     0,     0,     0,
       0,   108,     8,   194,    89,    92,    91,    79,    77,    94,
      88
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -230,  -230,  -230,  -230,  -230,  -230,  -230,  -230,    45,  -230,
     -72,    61,   -64,  -230,   -65,  -230,  -229,  -230,   -52,   -15,
     193,  -230,    -7,  -230,    -6,  -230,    -8,   -61,  -133,   245,
    -230,  -230,   159,  -230,  -230
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,    18,    19,    87,    20,    82,    89,    90,   273,
     274,   130,   131,   132,   164,   190,   165,   284,   166,   156,
     157,   200,   201,   204,   205,   321,    96,    40,    41,    21,
     207,    44,   123,   334,   350
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
     138,   174,    39,   184,   101,    42,    46,   185,   187,   168,
      83,    84,   169,   283,   176,   212,   213,   290,   191,   125,
     159,   127,    91,    22,    92,    93,    94,    95,   101,    43,
      97,   153,    85,   122,   116,   117,   118,   119,   120,   154,
     155,   243,   244,   133,   139,   142,   145,   148,   151,   152,
     158,   177,   167,    47,   171,   172,   192,    86,   178,   180,
     118,   119,   120,   330,   331,   194,   196,   197,   202,   203,
     206,   125,   159,   127,   170,   209,   210,   211,   125,   159,
     127,   160,   135,   136,   186,   161,   181,   162,   198,    88,
     216,   217,   218,   219,   220,   221,   222,   223,   224,   225,
     226,   227,   228,   229,   230,   231,   232,   233,   234,   235,
     236,   237,   238,   137,   141,   144,   147,   150,   160,   282,
     163,   208,   161,   258,   240,    23,    24,   135,   136,   183,
     318,   323,   241,   193,    25,    26,    27,    28,    29,   160,
     135,   136,    30,   161,   242,   245,   182,   125,   159,   127,
     160,   135,   136,   246,   161,    48,   101,   102,   103,   247,
     248,    31,   199,     5,     6,     7,     8,     9,   249,    32,
      33,   125,   159,   127,    34,    35,   188,   135,   136,   189,
     173,    36,   250,   252,    37,    38,   116,   117,   118,   119,
     120,   253,   254,   255,   291,   324,   307,   160,   135,   136,
     325,   161,   317,   322,   256,   257,   259,   272,   260,   261,
     263,   264,   320,   265,   267,   162,   269,   271,   279,   285,
      98,    99,   100,   302,   303,   304,   305,   306,   311,   308,
     312,   327,   326,   281,   335,   336,   287,   289,   -78,   293,
     295,   297,   299,   338,   301,   -76,   -84,  -141,   345,   340,
     343,   309,   310,   347,   346,   316,   316,   348,   278,   341,
     179,   202,   328,   206,    49,   329,   101,   102,   103,   332,
     104,   105,   106,   107,   108,   109,   349,     0,     0,     0,
       0,   239,     0,   344,     0,     0,     0,     0,     0,     0,
     110,   111,   112,   113,   114,   115,   116,   117,   118,   119,
     120,     0,   280,     0,     0,     0,   288,     0,   292,   294,
     296,   298,   300,   215,     0,     0,     0,     0,     0,     0,
       0,     2,     3,     0,   315,   -27,   -27,   -27,   -27,   -27,
     -27,   -27,   -27,   -27,   -27,   -27,   -27,   -27,   -27,   -27,
     -27,   -27,   -27,   -27,   -27,   -27,   -27,   -27,   -27,     0,
      98,    99,   100,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     4,   -27,   -27,   -27,   -27,   -27,     0,     0,
       5,     6,     7,     8,     9,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    10,    11,    12,    13,    14,
      98,     0,   -27,    15,     0,   -27,   101,   102,   103,     0,
     104,   105,   106,   107,   108,   109,    98,    99,   100,     0,
       0,     0,     0,   -27,     0,    16,    17,     0,     0,     0,
     110,   111,   112,   113,   114,   115,   116,   117,   118,   119,
     120,     0,     0,     0,     0,     0,   101,   102,   103,   214,
     104,   105,   106,   107,   108,   109,     0,     0,     0,     0,
       0,     0,   101,   102,   103,     0,   104,   105,   106,   107,
     108,   109,   112,   113,   114,   115,   116,   117,   118,   119,
     120,     0,     0,     0,     0,     0,   110,   111,   112,   113,
     114,   115,   116,   117,   118,   119,   120,     0,     0,     0,
       0,     0,     0,     0,     0,   333,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,    71,    72,    73,
       0,    98,    99,   100,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    74,    75,    76,    77,    78,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    98,    99,   100,
       0,     0,     0,    79,     0,     0,    80,   101,   102,   103,
       0,   104,   105,   106,   107,   108,   109,     0,     0,     0,
       0,     0,     0,     0,    81,     0,     0,     0,     0,     0,
       0,   110,   111,   112,   113,   114,   115,   116,   117,   118,
     119,   120,     0,   101,   102,   103,   251,   104,   105,   106,
     107,   108,   109,    98,    99,   100,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   110,   111,   112,
     113,   114,   115,   116,   117,   118,   119,   120,     0,     0,
       0,     0,   277,     0,     0,     0,     0,     0,    98,    99,
     100,     0,     0,     0,     0,     0,     0,     0,     0,   101,
     102,   103,     0,   104,   105,   106,   107,   108,   109,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   110,   111,   112,   113,   114,   115,   116,
     117,   118,   119,   120,   101,   102,   103,   121,   104,   105,
     106,   107,   108,   109,    98,    99,   100,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   110,   111,
     112,   113,   114,   115,   116,   117,   118,   119,   120,     0,
       0,     0,   124,     0,     0,     0,     0,     0,     0,    98,
      99,   100,     0,     0,     0,     0,     0,     0,     0,     0,
     101,   102,   103,     0,   104,   105,   106,   107,   108,   109,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   110,   111,   112,   113,   114,   115,
     116,   117,   118,   119,   120,   101,   102,   103,   268,   104,
     105,   106,   107,   108,   109,    98,    99,   100,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   110,
     111,   112,   113,   114,   115,   116,   117,   118,   119,   120,
       0,     0,     0,   270,     0,     0,     0,     0,     0,     0,
      98,    99,   100,     0,     0,     0,     0,     0,     0,     0,
       0,   101,   102,   103,     0,   104,   105,   106,   107,   108,
     109,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   101,   102,   103,   275,
     104,   105,   106,   107,   108,   109,    98,    99,   100,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     110,   111,   112,   113,   114,   115,   116,   117,   118,   119,
     120,     0,     0,     0,   276,     0,     0,     0,     0,     0,
       0,    98,    99,   100,     0,     0,     0,     0,     0,     0,
       0,     0,   101,   102,   103,     0,   104,   105,   106,   107,
     108,   109,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   110,   111,   112,   113,
     114,   115,   116,   117,   118,   119,   120,   101,   102,   103,
     342,   104,   105,   106,   107,   108,   109,    98,     0,   100,
     101,   102,   103,     0,   104,   105,   106,   107,   108,   109,
       0,   110,   111,   112,   113,   114,   115,   116,   117,   118,
     119,   120,     0,     0,     0,     0,     0,   113,   114,   115,
     116,   117,   118,   119,   120,     0,     0,     0,     0,     0,
       0,     0,     0,   101,   102,   103,     0,   104,   105,   106,
     107,   108,   109,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   111,   112,
     113,   114,   115,   116,   117,   118,   119,   120,   125,   134,
     127,     0,   135,   136,     0,     0,     0,     0,     0,     0,
      23,    24,     0,     0,     0,     0,     0,     0,     0,    25,
      26,    27,    28,    29,     0,     0,     0,    30,   125,   126,
     127,     0,   128,     0,     0,     0,     0,     0,     0,     0,
      23,    24,     0,     0,     0,     0,    31,     0,     0,    25,
      26,    27,    28,    29,    32,    33,     0,    30,     0,    34,
      35,     0,     0,     0,     0,     0,   129,     0,     0,    37,
      38,     0,     0,     0,     0,     0,    31,     0,     0,     0,
     125,   159,   127,     0,    32,    33,     0,     0,   313,    34,
      35,     0,    23,    24,     0,     0,   129,     0,     0,    37,
      38,    25,    26,    27,    28,    29,     0,     0,     0,    30,
     125,   140,   127,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    23,    24,     0,     0,     0,     0,    31,     0,
       0,    25,    26,    27,    28,    29,    32,    33,     0,    30,
       0,    34,    35,     0,     0,     0,     0,     0,   314,     0,
       0,    37,    38,     0,     0,     0,     0,     0,    31,     0,
       0,     0,   125,   143,   127,     0,    32,    33,     0,     0,
       0,    34,    35,     0,    23,    24,     0,     0,   129,     0,
       0,    37,    38,    25,    26,    27,    28,    29,     0,     0,
       0,    30,   125,   146,   127,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    23,    24,     0,     0,     0,     0,
      31,     0,     0,    25,    26,    27,    28,    29,    32,    33,
       0,    30,     0,    34,    35,     0,     0,     0,     0,     0,
     129,     0,     0,    37,    38,     0,     0,     0,     0,     0,
      31,     0,     0,     0,   125,   149,   127,     0,    32,    33,
       0,     0,     0,    34,    35,     0,    23,    24,     0,     0,
     129,     0,     0,    37,    38,    25,    26,    27,    28,    29,
       0,     0,     0,    30,   125,   159,   127,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    23,    24,     0,     0,
       0,     0,    31,     0,     0,    25,    26,    27,    28,    29,
      32,    33,     0,    30,     0,    34,    35,     0,     0,     0,
       0,     0,   129,     0,     0,    37,    38,     0,     0,     0,
       0,     0,    31,     0,     0,     0,     0,     0,     0,     0,
      32,    33,     0,     0,     0,    34,    35,     0,     0,     0,
       0,   153,   129,   135,   136,    37,    38,     0,     0,   154,
     155,    23,    24,     0,     0,     0,     0,     0,     0,     0,
      25,    26,    27,    28,    29,     0,     0,     0,    30,     0,
     153,     0,     0,     0,     0,     0,     0,     0,   154,   155,
      23,    24,     0,     0,     0,     0,     0,    31,     0,    25,
      26,    27,    28,    29,     0,    32,    33,    30,     0,     0,
      34,    35,     0,     0,     0,     0,     0,   175,     0,     0,
      37,    38,     0,     0,     0,     0,    31,     0,     0,     0,
       0,     0,   153,     0,    32,    33,     0,     0,     0,    34,
      35,   155,    23,    24,     0,     0,    36,     0,     0,    37,
      38,    25,    26,    27,    28,    29,     0,     0,     0,    30,
       0,   266,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    23,    24,     0,     0,     0,     0,     0,    31,     0,
      25,    26,    27,    28,    29,     0,    32,    33,    30,     0,
       0,    34,    35,     0,     0,     0,     0,     0,    36,     0,
       0,    37,    38,     0,     0,     0,     0,    31,     0,     0,
       0,     0,     0,   337,     0,    32,    33,     0,     0,     0,
      34,    35,     0,    23,    24,     0,     0,    36,     0,     0,
      37,    38,    25,    26,    27,    28,    29,     0,     0,     0,
      30,     0,     0,   262,   243,   244,     0,     0,     0,     0,
       0,     0,    23,    24,     0,     0,     0,     0,     0,    31,
       0,    25,    26,    27,    28,    29,     0,    32,    33,    30,
       0,     0,    34,    35,     0,     0,     0,     0,     0,    36,
       0,     0,    37,    38,     0,     0,     0,     0,    31,     0,
       0,     0,     0,     0,     0,     0,    32,    33,     0,     0,
       0,    34,    35,     0,   339,   243,   244,     0,    36,     0,
       0,    37,    38,    23,    24,     0,     0,     0,     0,     0,
       0,     0,    25,    26,    27,    28,    29,     0,     0,     0,
      30,     0,     0,     0,   135,   136,     0,     0,     0,     0,
       0,     0,    23,    24,     0,     0,     0,     0,     0,    31,
       0,    25,    26,    27,    28,    29,     0,    32,    33,    30,
       0,     0,    34,    35,     0,     0,     0,     0,     0,    36,
       0,     0,    37,    38,     0,     0,     0,     0,    31,   319,
       0,     0,     0,     0,     0,     0,    32,    33,   243,   244,
       0,    34,    35,     0,     0,     0,    23,    24,    36,     0,
       0,    37,    38,     0,     0,    25,    26,    27,    28,    29,
     135,   136,     0,    30,     0,     0,     0,     0,    23,    24,
       0,     0,     0,     0,     0,     0,     0,    25,    26,    27,
      28,    29,    31,     0,     0,    30,     0,     0,     0,     0,
      32,    33,     0,     0,     0,    34,    35,     0,    23,    24,
       0,     0,    36,     0,    31,    37,    38,    25,    26,    27,
      28,    29,    32,    33,     0,    30,     0,    34,    35,     0,
      23,    24,     0,     0,    36,     0,     0,    37,    38,    25,
      26,    27,    28,    29,    31,     0,     0,    30,     0,     0,
       0,     0,    32,    33,     0,     0,     0,    34,    35,     0,
      45,     0,     0,     0,    36,     0,    31,    37,    38,     0,
       0,     0,     0,     0,    32,    33,     0,     0,     0,    34,
      35,     0,   195,    23,    24,     0,    36,     0,     0,    37,
      38,     0,    25,    26,    27,    28,    29,     0,     0,     0,
      30,    23,    24,     0,     0,     0,     0,     0,     0,     0,
      25,    26,    27,    28,    29,     0,     0,     0,    30,    31,
       0,     0,     0,     0,     0,     0,     0,    32,    33,     0,
       0,     0,    34,    35,     0,     0,     0,    31,     0,    36,
     286,     0,    37,    38,     0,    32,    33,     0,     0,     0,
      34,    35,     0,     0,     0,     0,     0,    36,     0,     0,
      37,    38,   101,   102,   103,     0,   104,   105,     0,   107,
     108,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     114,   115,   116,   117,   118,   119,   120
};

static const yytype_int16 yycheck[] =
{
      52,    65,    10,    68,    54,    92,    14,    68,    69,    31,
      47,    48,    34,   242,    66,    92,    93,   246,    70,    28,
      29,    30,    30,    70,    32,    33,    34,    35,    54,    92,
      38,    30,    69,    73,    84,    85,    86,    87,    88,    38,
      39,    32,    33,    51,    52,    53,    54,    55,    56,    57,
      58,    66,    60,    70,    62,    63,    71,    94,    66,    67,
      86,    87,    88,    92,    93,    73,    74,    75,    76,    77,
      78,    28,    29,    30,    96,    83,    84,    85,    28,    29,
      30,    31,    32,    33,    96,    35,    36,    96,     3,    41,
      98,    99,   100,   101,   102,   103,   104,   105,   106,   107,
     108,   109,   110,   111,   112,   113,   114,   115,   116,   117,
     118,   119,   120,    52,    53,    54,    55,    56,    31,    32,
      59,    92,    35,   175,    92,    40,    41,    32,    33,    68,
     263,   264,    93,    72,    49,    50,    51,    52,    53,    31,
      32,    33,    57,    35,    93,    93,    96,    28,    29,    30,
      31,    32,    33,    93,    35,    41,    54,    55,    56,    93,
      93,    76,    77,    49,    50,    51,    52,    53,    93,    84,
      85,    28,    29,    30,    89,    90,    31,    32,    33,    34,
      37,    96,    93,    93,    99,   100,    84,    85,    86,    87,
      88,    93,    93,    35,   246,    29,   257,    31,    32,    33,
     265,    35,   263,   264,    93,    93,    93,    73,    93,    93,
      93,    93,   264,    93,    93,    96,    93,    93,    92,    97,
       8,     9,    10,    32,    34,    97,    96,    96,    29,    97,
      97,    29,    97,   241,    93,    30,   244,   245,    92,   247,
     248,   249,   250,    93,   252,    92,    92,    92,    97,    93,
      98,   259,   260,    29,    97,   263,   264,    97,   213,   331,
      67,   269,   269,   271,    19,   271,    54,    55,    56,   277,
      58,    59,    60,    61,    62,    63,   340,    -1,    -1,    -1,
      -1,   122,    -1,   335,    -1,    -1,    -1,    -1,    -1,    -1,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    -1,   241,    -1,    -1,    -1,   245,    -1,   247,   248,
     249,   250,   251,   101,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,     0,     1,    -1,   263,     4,     5,     6,     7,     8,
       9,    10,    11,    12,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    -1,
       8,     9,    10,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    41,    42,    43,    44,    45,    46,    -1,    -1,
      49,    50,    51,    52,    53,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    64,    65,    66,    67,    68,
       8,    -1,    71,    72,    -1,    74,    54,    55,    56,    -1,
      58,    59,    60,    61,    62,    63,     8,     9,    10,    -1,
      -1,    -1,    -1,    92,    -1,    94,    95,    -1,    -1,    -1,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    -1,    -1,    -1,    -1,    -1,    54,    55,    56,    97,
      58,    59,    60,    61,    62,    63,    -1,    -1,    -1,    -1,
      -1,    -1,    54,    55,    56,    -1,    58,    59,    60,    61,
      62,    63,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    -1,    -1,    -1,    -1,    -1,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    97,     4,     5,     6,     7,
       8,     9,    10,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      -1,     8,     9,    10,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    42,    43,    44,    45,    46,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,     8,     9,    10,
      -1,    -1,    -1,    71,    -1,    -1,    74,    54,    55,    56,
      -1,    58,    59,    60,    61,    62,    63,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    92,    -1,    -1,    -1,    -1,    -1,
      -1,    78,    79,    80,    81,    82,    83,    84,    85,    86,
      87,    88,    -1,    54,    55,    56,    93,    58,    59,    60,
      61,    62,    63,     8,     9,    10,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    78,    79,    80,
      81,    82,    83,    84,    85,    86,    87,    88,    -1,    -1,
      -1,    -1,    93,    -1,    -1,    -1,    -1,    -1,     8,     9,
      10,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    54,
      55,    56,    -1,    58,    59,    60,    61,    62,    63,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    54,    55,    56,    92,    58,    59,
      60,    61,    62,    63,     8,     9,    10,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    78,    79,
      80,    81,    82,    83,    84,    85,    86,    87,    88,    -1,
      -1,    -1,    92,    -1,    -1,    -1,    -1,    -1,    -1,     8,
       9,    10,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      54,    55,    56,    -1,    58,    59,    60,    61,    62,    63,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    78,    79,    80,    81,    82,    83,
      84,    85,    86,    87,    88,    54,    55,    56,    92,    58,
      59,    60,    61,    62,    63,     8,     9,    10,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      -1,    -1,    -1,    92,    -1,    -1,    -1,    -1,    -1,    -1,
       8,     9,    10,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    54,    55,    56,    -1,    58,    59,    60,    61,    62,
      63,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    78,    79,    80,    81,    82,
      83,    84,    85,    86,    87,    88,    54,    55,    56,    92,
      58,    59,    60,    61,    62,    63,     8,     9,    10,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    -1,    -1,    -1,    92,    -1,    -1,    -1,    -1,    -1,
      -1,     8,     9,    10,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    54,    55,    56,    -1,    58,    59,    60,    61,
      62,    63,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    54,    55,    56,
      92,    58,    59,    60,    61,    62,    63,     8,    -1,    10,
      54,    55,    56,    -1,    58,    59,    60,    61,    62,    63,
      -1,    78,    79,    80,    81,    82,    83,    84,    85,    86,
      87,    88,    -1,    -1,    -1,    -1,    -1,    81,    82,    83,
      84,    85,    86,    87,    88,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    54,    55,    56,    -1,    58,    59,    60,
      61,    62,    63,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    79,    80,
      81,    82,    83,    84,    85,    86,    87,    88,    28,    29,
      30,    -1,    32,    33,    -1,    -1,    -1,    -1,    -1,    -1,
      40,    41,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    49,
      50,    51,    52,    53,    -1,    -1,    -1,    57,    28,    29,
      30,    -1,    32,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      40,    41,    -1,    -1,    -1,    -1,    76,    -1,    -1,    49,
      50,    51,    52,    53,    84,    85,    -1,    57,    -1,    89,
      90,    -1,    -1,    -1,    -1,    -1,    96,    -1,    -1,    99,
     100,    -1,    -1,    -1,    -1,    -1,    76,    -1,    -1,    -1,
      28,    29,    30,    -1,    84,    85,    -1,    -1,    36,    89,
      90,    -1,    40,    41,    -1,    -1,    96,    -1,    -1,    99,
     100,    49,    50,    51,    52,    53,    -1,    -1,    -1,    57,
      28,    29,    30,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    40,    41,    -1,    -1,    -1,    -1,    76,    -1,
      -1,    49,    50,    51,    52,    53,    84,    85,    -1,    57,
      -1,    89,    90,    -1,    -1,    -1,    -1,    -1,    96,    -1,
      -1,    99,   100,    -1,    -1,    -1,    -1,    -1,    76,    -1,
      -1,    -1,    28,    29,    30,    -1,    84,    85,    -1,    -1,
      -1,    89,    90,    -1,    40,    41,    -1,    -1,    96,    -1,
      -1,    99,   100,    49,    50,    51,    52,    53,    -1,    -1,
      -1,    57,    28,    29,    30,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    40,    41,    -1,    -1,    -1,    -1,
      76,    -1,    -1,    49,    50,    51,    52,    53,    84,    85,
      -1,    57,    -1,    89,    90,    -1,    -1,    -1,    -1,    -1,
      96,    -1,    -1,    99,   100,    -1,    -1,    -1,    -1,    -1,
      76,    -1,    -1,    -1,    28,    29,    30,    -1,    84,    85,
      -1,    -1,    -1,    89,    90,    -1,    40,    41,    -1,    -1,
      96,    -1,    -1,    99,   100,    49,    50,    51,    52,    53,
      -1,    -1,    -1,    57,    28,    29,    30,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    40,    41,    -1,    -1,
      -1,    -1,    76,    -1,    -1,    49,    50,    51,    52,    53,
      84,    85,    -1,    57,    -1,    89,    90,    -1,    -1,    -1,
      -1,    -1,    96,    -1,    -1,    99,   100,    -1,    -1,    -1,
      -1,    -1,    76,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      84,    85,    -1,    -1,    -1,    89,    90,    -1,    -1,    -1,
      -1,    30,    96,    32,    33,    99,   100,    -1,    -1,    38,
      39,    40,    41,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      49,    50,    51,    52,    53,    -1,    -1,    -1,    57,    -1,
      30,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    38,    39,
      40,    41,    -1,    -1,    -1,    -1,    -1,    76,    -1,    49,
      50,    51,    52,    53,    -1,    84,    85,    57,    -1,    -1,
      89,    90,    -1,    -1,    -1,    -1,    -1,    96,    -1,    -1,
      99,   100,    -1,    -1,    -1,    -1,    76,    -1,    -1,    -1,
      -1,    -1,    30,    -1,    84,    85,    -1,    -1,    -1,    89,
      90,    39,    40,    41,    -1,    -1,    96,    -1,    -1,    99,
     100,    49,    50,    51,    52,    53,    -1,    -1,    -1,    57,
      -1,    30,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    40,    41,    -1,    -1,    -1,    -1,    -1,    76,    -1,
      49,    50,    51,    52,    53,    -1,    84,    85,    57,    -1,
      -1,    89,    90,    -1,    -1,    -1,    -1,    -1,    96,    -1,
      -1,    99,   100,    -1,    -1,    -1,    -1,    76,    -1,    -1,
      -1,    -1,    -1,    30,    -1,    84,    85,    -1,    -1,    -1,
      89,    90,    -1,    40,    41,    -1,    -1,    96,    -1,    -1,
      99,   100,    49,    50,    51,    52,    53,    -1,    -1,    -1,
      57,    -1,    -1,    31,    32,    33,    -1,    -1,    -1,    -1,
      -1,    -1,    40,    41,    -1,    -1,    -1,    -1,    -1,    76,
      -1,    49,    50,    51,    52,    53,    -1,    84,    85,    57,
      -1,    -1,    89,    90,    -1,    -1,    -1,    -1,    -1,    96,
      -1,    -1,    99,   100,    -1,    -1,    -1,    -1,    76,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    84,    85,    -1,    -1,
      -1,    89,    90,    -1,    31,    32,    33,    -1,    96,    -1,
      -1,    99,   100,    40,    41,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    49,    50,    51,    52,    53,    -1,    -1,    -1,
      57,    -1,    -1,    -1,    32,    33,    -1,    -1,    -1,    -1,
      -1,    -1,    40,    41,    -1,    -1,    -1,    -1,    -1,    76,
      -1,    49,    50,    51,    52,    53,    -1,    84,    85,    57,
      -1,    -1,    89,    90,    -1,    -1,    -1,    -1,    -1,    96,
      -1,    -1,    99,   100,    -1,    -1,    -1,    -1,    76,    77,
      -1,    -1,    -1,    -1,    -1,    -1,    84,    85,    32,    33,
      -1,    89,    90,    -1,    -1,    -1,    40,    41,    96,    -1,
      -1,    99,   100,    -1,    -1,    49,    50,    51,    52,    53,
      32,    33,    -1,    57,    -1,    -1,    -1,    -1,    40,    41,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    49,    50,    51,
      52,    53,    76,    -1,    -1,    57,    -1,    -1,    -1,    -1,
      84,    85,    -1,    -1,    -1,    89,    90,    -1,    40,    41,
      -1,    -1,    96,    -1,    76,    99,   100,    49,    50,    51,
      52,    53,    84,    85,    -1,    57,    -1,    89,    90,    -1,
      40,    41,    -1,    -1,    96,    -1,    -1,    99,   100,    49,
      50,    51,    52,    53,    76,    -1,    -1,    57,    -1,    -1,
      -1,    -1,    84,    85,    -1,    -1,    -1,    89,    90,    -1,
      92,    -1,    -1,    -1,    96,    -1,    76,    99,   100,    -1,
      -1,    -1,    -1,    -1,    84,    85,    -1,    -1,    -1,    89,
      90,    -1,    92,    40,    41,    -1,    96,    -1,    -1,    99,
     100,    -1,    49,    50,    51,    52,    53,    -1,    -1,    -1,
      57,    40,    41,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      49,    50,    51,    52,    53,    -1,    -1,    -1,    57,    76,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    84,    85,    -1,
      -1,    -1,    89,    90,    -1,    -1,    -1,    76,    -1,    96,
      97,    -1,    99,   100,    -1,    84,    85,    -1,    -1,    -1,
      89,    90,    -1,    -1,    -1,    -1,    -1,    96,    -1,    -1,
      99,   100,    54,    55,    56,    -1,    58,    59,    -1,    61,
      62,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      82,    83,    84,    85,    86,    87,    88
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,   103,     0,     1,    41,    49,    50,    51,    52,    53,
      64,    65,    66,    67,    68,    72,    94,    95,   104,   105,
     107,   131,    70,    40,    41,    49,    50,    51,    52,    53,
      57,    76,    84,    85,    89,    90,    96,    99,   100,   128,
     129,   130,    92,    92,   133,    92,   128,    70,    41,   131,
       4,     5,     6,     7,     8,     9,    10,    11,    12,    13,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,    42,    43,    44,    45,    46,    71,
      74,    92,   108,    47,    48,    69,    94,   106,    41,   109,
     110,   128,   128,   128,   128,   128,   128,   128,     8,     9,
      10,    54,    55,    56,    58,    59,    60,    61,    62,    63,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    92,    73,   134,    92,    28,    29,    30,    32,    96,
     113,   114,   115,   128,    29,    32,    33,   113,   120,   128,
      29,   113,   128,    29,   113,   128,    29,   113,   128,    29,
     113,   128,   128,    30,    38,    39,   121,   122,   128,    29,
      31,    35,    96,   113,   116,   118,   120,   128,    31,    34,
      96,   128,   128,    37,   114,    96,   120,   121,   128,   122,
     128,    36,    96,   113,   116,   129,    96,   129,    31,    34,
     117,   120,   121,   113,   128,    92,   128,   128,     3,    77,
     123,   124,   128,   128,   125,   126,   128,   132,    92,   128,
     128,   128,    92,    93,    97,   101,   128,   128,   128,   128,
     128,   128,   128,   128,   128,   128,   128,   128,   128,   128,
     128,   128,   128,   128,   128,   128,   128,   128,   128,   134,
      92,    93,    93,    32,    33,    93,    93,    93,    93,    93,
      93,    93,    93,    93,    93,    35,    93,    93,   120,    93,
      93,    93,    31,    93,    93,    93,    30,    93,    92,    93,
      92,    93,    73,   111,   112,    92,    92,    93,   110,    92,
     113,   128,    32,   118,   119,    97,    97,   128,   113,   128,
     118,   120,   113,   128,   113,   128,   113,   128,   113,   128,
     113,   128,    32,    34,    97,    96,    96,   129,    97,   128,
     128,    29,    97,    36,    96,   113,   128,   129,   130,    77,
     120,   127,   129,   130,    29,   116,    97,    29,   124,   126,
      92,    93,   128,    97,   135,    93,    30,    30,    93,    31,
      93,   112,    92,    98,   120,    97,    97,    29,    97,   114,
     136
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,   102,   103,   103,   104,   104,   104,   104,   104,   104,
     104,   104,   104,   104,   104,   104,   104,   104,   104,   104,
     104,   104,   104,   105,   105,   106,   106,   107,   107,   107,
     108,   108,   108,   108,   108,   108,   108,   108,   108,   108,
     108,   108,   108,   108,   108,   108,   108,   108,   108,   108,
     108,   108,   108,   108,   108,   108,   108,   108,   108,   108,
     108,   108,   108,   108,   108,   108,   108,   108,   108,   108,
     108,   108,   108,   108,   108,   108,   108,   108,   108,   108,
     108,   108,   108,   108,   108,   108,   108,   108,   108,   108,
     108,   108,   108,   108,   108,   108,   108,   108,   108,   108,
     108,   108,   109,   109,   109,   110,   111,   111,   111,   112,
     113,   113,   114,   114,   114,   115,   115,   115,   116,   116,
     117,   117,   117,   118,   118,   119,   119,   120,   120,   121,
     121,   122,   122,   123,   123,   124,   124,   124,   125,   125,
     126,   127,   127,   128,   128,   129,   130,   130,   130,   130,
     130,   130,   130,   130,   130,   130,   130,   130,   130,   130,
     130,   130,   130,   130,   130,   130,   130,   130,   130,   130,
     130,   130,   130,   130,   130,   130,   130,   130,   130,   130,
     130,   130,   130,   130,   131,   131,   131,   131,   131,   131,
     132,   133,   134,   135,   136
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     0,     2,     2,     3,     4,     4,     6,     3,
       2,     2,     3,     4,     4,     5,     4,     2,     3,     4,
       2,     5,     1,     1,     1,     0,     1,     0,     2,     2,
       1,     2,     2,     2,     2,     4,     2,     4,     2,     2,
       2,     2,     4,     4,     4,     4,     2,     4,     2,     4,
       2,     2,     2,     2,     4,     4,     4,     4,     2,     2,
       4,     4,     4,     2,     2,     4,     4,     4,     2,     4,
       2,     4,     2,     1,     2,     4,     4,     6,     4,     6,
       4,     4,     4,     4,     4,     4,     4,     4,     7,     6,
       4,     6,     6,     4,     6,     2,     2,     1,     2,     2,
       2,     1,     0,     1,     3,     1,     0,     1,     3,     1,
       1,     1,     1,     1,     1,     3,     4,     3,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     3,     1,     1,     1,     1,     3,
       1,     1,     1,     1,     1,     3,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     2,
       2,     2,     2,     2,     1,     1,     1,     1,     1,     1,
       0,     0,     0,     0,     0
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)




# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif






/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 4: /* statement: label.part '\n'  */
#line 936 "zmac.y"
                        {
		if ((yyvsp[-1].itemptr)) list(dollarsign);
		else  list1();
	}
#line 2805 "y.tab.c"
    break;

  case 5: /* statement: label.part operation '\n'  */
#line 941 "zmac.y"
                                  {
		list(dollarsign);
	}
#line 2813 "y.tab.c"
    break;

  case 6: /* statement: symbol EQU expression '\n'  */
#line 945 "zmac.y"
                                   {
		/* a forward reference to a label in the expression cannot
		 * be fixed in a forward reference to the EQU;
		 * it would need three passes. -rjm
		 */
		if(!pass2 && equ_bad_label) {
			/* this indicates that the equ has an incorrect
			 * (i.e. pass 1) value.
			 */
			(yyvsp[-3].itemptr)->i_equbad = 1;
		} else {
			/* but if 2nd pass or no forward reference, it's ok. */
			(yyvsp[-3].itemptr)->i_equbad = 0;
		}
		equ_bad_label=0;
		switch((yyvsp[-3].itemptr)->i_token) {
		case UNDECLARED: case WASEQUATED:
			(yyvsp[-3].itemptr)->i_token = EQUATED;
			(yyvsp[-3].itemptr)->i_value = (yyvsp[-1].ival);
			break;
		case EQUATED:
			if ((yyvsp[-3].itemptr)->i_value == (yyvsp[-1].ival))
				break; /* Allow benign redefinition -mgr */
			/* Drop-through intentional */
		default:
			err[mflag]++;
			(yyvsp[-3].itemptr)->i_token = MULTDEF;
		}
		list((yyvsp[-1].ival));
	}
#line 2848 "y.tab.c"
    break;

  case 7: /* statement: symbol DEFL expression '\n'  */
#line 976 "zmac.y"
                                    {
		switch((yyvsp[-3].itemptr)->i_token) {
		case UNDECLARED: case DEFLED:
			(yyvsp[-3].itemptr)->i_token = DEFLED;
			(yyvsp[-3].itemptr)->i_value = (yyvsp[-1].ival);
			break;
		default:
			err[mflag]++;
			(yyvsp[-3].itemptr)->i_token = MULTDEF;
		}
		list((yyvsp[-1].ival));
	}
#line 2865 "y.tab.c"
    break;

  case 8: /* statement: symbol MINMAX expression ',' expression '\n'  */
#line 989 "zmac.y"
                                                     {
		switch ((yyvsp[-5].itemptr)->i_token) {
		case UNDECLARED: case DEFLED:
			(yyvsp[-5].itemptr)->i_token = DEFLED;
			if ((yyvsp[-4].itemptr)->i_value)	/* max */
				list((yyvsp[-5].itemptr)->i_value = ((yyvsp[-3].ival) > (yyvsp[-1].ival)? (yyvsp[-3].ival):(yyvsp[-1].ival)));
			else list((yyvsp[-5].itemptr)->i_value = ((yyvsp[-3].ival) < (yyvsp[-1].ival)? (yyvsp[-3].ival):(yyvsp[-1].ival)));
			break;
		default:
			err[mflag]++;
			(yyvsp[-5].itemptr)->i_token = MULTDEF;
			list((yyvsp[-5].itemptr)->i_value);
		}
	}
#line 2884 "y.tab.c"
    break;

  case 9: /* statement: IF expression '\n'  */
#line 1004 "zmac.y"
                           {
		/* all $2's here were yypc[2].ival before.
		 * I think the idea was perhaps to allow constants
		 * only...? Anyway, it now allows any expression -
		 * which would seem to make sense given the definition
		 * above, right? :-)  -rjm
		 */
		if (ifptr >= ifstmax)
			error("Too many ifs");
		else {
			if (pass2) {
				*++ifptr = *expifp++;
				if (*ifptr != !((yyvsp[-1].ival))) err[pflag]++;
			} else {
				if (expifp >= expifmax)
					error("Too many ifs!");
				*expifp++ = !((yyvsp[-1].ival));
				*++ifptr = !((yyvsp[-1].ival));
			}
		}
		saveopt = fopt;
		fopt = 1;
		list((yyvsp[-1].ival));
		fopt = saveopt;
	}
#line 2914 "y.tab.c"
    break;

  case 10: /* statement: ELSE '\n'  */
#line 1030 "zmac.y"
                  {
		/* FIXME: it would be nice to spot repeated ELSEs, but how? */
		*ifptr = !*ifptr;
		saveopt = fopt;
		fopt = 1;
		list1();
		fopt = saveopt;
	}
#line 2927 "y.tab.c"
    break;

  case 11: /* statement: ENDIF '\n'  */
#line 1039 "zmac.y"
                   {
		if (ifptr == ifstack) err[bflag]++;
		else --ifptr;
		list1();
	}
#line 2937 "y.tab.c"
    break;

  case 12: /* statement: label.part END '\n'  */
#line 1045 "zmac.y"
                            {
		list(dollarsign);
		peekc = 0;
	}
#line 2946 "y.tab.c"
    break;

  case 13: /* statement: label.part END expression '\n'  */
#line 1050 "zmac.y"
                                       {
		xeq_flag++;
		xeq = (yyvsp[-1].ival);
		list((yyvsp[-1].ival));
		peekc = 0;
	}
#line 2957 "y.tab.c"
    break;

  case 14: /* statement: label.part DEFS expression '\n'  */
#line 1057 "zmac.y"
                                        {
		if ((yyvsp[-1].ival) < 0) err[vflag]++;
		list(dollarsign);
		if ((yyvsp[-1].ival)) {
			flushbin();
			dollarsign += (yyvsp[-1].ival);
			olddollar = dollarsign;

			/* if it's not hex output though, we also need
			 * to output zeroes as appropriate. -rjm
			 */
			if(!output_hex && pass2) {
				int f;
				for (f=0;f<((yyvsp[-1].ival));f++)
					fputc(0, fbuf);
			}
		}
	}
#line 2980 "y.tab.c"
    break;

  case 15: /* statement: ARGPSEUDO arg_on ARG arg_off '\n'  */
#line 1076 "zmac.y"
                                          {
		list1();
		switch ((yyvsp[-4].itemptr)->i_value) {

		case 0:		/* title */
			lineptr = linebuf;
			cp = tempbuf;
			title = titlespace;
			while ((*title++ = *cp++) && (title < &titlespace[TITLELEN]));
			*title = 0;
			title = titlespace;
			break;

		case 1:		/* rsym */
			if (pass2) break;
			insymtab(tempbuf);
			break;

		case 2:		/* wsym */
			strcpy(writesyms, tempbuf);
			break;

		case 3:		/* include file */
			if (*tempbuf == '"' || *tempbuf == '\'')
			{
				if (tempbuf[strlen (tempbuf) - 1] == '"' || tempbuf[strlen (tempbuf) - 1] == '\'')
					tempbuf[strlen (tempbuf) - 1] = 0;
				next_source(tempbuf + 1) ;
			}
			else
			{
				next_source(tempbuf) ;
			}
			break ;
		}
	}
#line 3021 "y.tab.c"
    break;

  case 16: /* statement: ARGPSEUDO arg_on arg_off '\n'  */
#line 1113 "zmac.y"
                                      {
		fprintf(stderr,"ARGPSEUDO error\n");
		err[fflag]++;
		list(dollarsign);
	}
#line 3031 "y.tab.c"
    break;

  case 17: /* statement: LIST '\n'  */
#line 1119 "zmac.y"
                  {
		list_tmp1=(yyvsp[-1].itemptr)->i_value;
		list_tmp2=1;
		goto dolopt;
	}
#line 3041 "y.tab.c"
    break;

  case 18: /* statement: LIST expression '\n'  */
#line 1125 "zmac.y"
                             {
		list_tmp1=(yyvsp[-2].itemptr)->i_value;
		list_tmp2=(yyvsp[-1].ival);
	dolopt:
		linecnt++;
		if (pass2) {
			lineptr = linebuf;
			switch (list_tmp1) {
			case 0:	/* list */
				if (list_tmp2 < 0) lstoff = 1;
				if (list_tmp2 > 0) lstoff = 0;
				break;

			case 1:	/* eject */
				if (list_tmp2) eject();
				break;

			case 2:	/* space */
				if ((line + list_tmp2) > 60) eject();
				else space(list_tmp2);
				break;

			case 3:	/* elist */
				eopt = edef;
				if (list_tmp2 < 0) eopt = 0;
				if (list_tmp2 > 0) eopt = 1;
				break;

			case 4:	/* fopt */
				fopt = fdef;
				if (list_tmp2 < 0) fopt = 0;
				if (list_tmp2 > 0) fopt = 1;
				break;

			case 5:	/* gopt */
				gopt = gdef;
				if (list_tmp2 < 0) gopt = 1;
				if (list_tmp2 > 0) gopt = 0;
				break;

			case 6: /* mopt */
				mopt = mdef;
				if (list_tmp2 < 0) mopt = 0;
				if (list_tmp2 > 0) mopt = 1;
			}
		}
	}
#line 3093 "y.tab.c"
    break;

  case 19: /* statement: UNDECLARED MACRO parm.list '\n'  */
#line 1173 "zmac.y"
                                        {
		(yyvsp[-3].itemptr)->i_token = MNAME;
		(yyvsp[-3].itemptr)->i_value = mfptr;
#ifdef M_DEBUG
		fprintf (stderr, "[UNDECLARED MACRO %s]\n", (yyvsp[-3].itemptr)->i_string);
#endif
		mfseek(mfile, (long)mfptr, 0);
		list1();
		mlex() ;
		parm_number = 0;
	}
#line 3109 "y.tab.c"
    break;

  case 20: /* statement: OLDMNAME MACRO  */
#line 1185 "zmac.y"
                       {
		(yyvsp[-1].itemptr)->i_token = MNAME;
#ifdef M_DEBUG
		fprintf (stderr, "[OLDNAME MACRO %s]\n", (yyvsp[-1].itemptr)->i_string);
#endif
		while (yychar != ENDM && yychar) {
			while (yychar != '\n' && yychar)
				yychar = yylex();
			list1();
			yychar = yylex();
		}
		while (yychar != '\n' && yychar) yychar = yylex();
		list1();
		yychar = yylex();
	}
#line 3129 "y.tab.c"
    break;

  case 21: /* statement: label.part MNAME al arg.list '\n'  */
#line 1201 "zmac.y"
                                          {
#ifdef M_DEBUG
		fprintf (stderr, "[MNAME %s]\n", (yyvsp[-3].itemptr)->i_string);
#endif
		(yyvsp[-3].itemptr)->i_uses++ ;
		arg_flag = 0;
		parm_number = 0;
		list(dollarsign);
		expptr++;
		est = est2;
		est2 = NULL;
		est[FLOC] = floc;
		est[TEMPNUM] = (char *)(long)exp_number++;
		floc = (char *)(long)((yyvsp[-3].itemptr)->i_value);
		mfseek(mfile, (long)floc, 0);
	}
#line 3150 "y.tab.c"
    break;

  case 22: /* statement: error  */
#line 1218 "zmac.y"
              {
		err[fflag]++;
		quoteflag = 0;
		arg_flag = 0;
		parm_number = 0;

		if (est2)
		{
			int i;
			for (i=0; i<PARMMAX; i++) {
				if (est2[i])
#ifdef M_DEBUG
	fprintf (stderr, "[Freeing2 arg%u(%p)]\n", i, est2[i]),
#endif
						free(est2[i]);
			}
			free(est2);
			est2 = NULL;
		}

		while(yychar != '\n' && yychar != '\0') yychar = yylex();
		list(dollarsign);
		yyclearin;yyerrok;
	}
#line 3179 "y.tab.c"
    break;

  case 27: /* label.part: %empty  */
#line 1258 "zmac.y"
        {	(yyval.itemptr) = NULL;	}
#line 3185 "y.tab.c"
    break;

  case 28: /* label.part: colonordot symbol  */
#line 1260 "zmac.y"
                          {
		switch((yyvsp[0].itemptr)->i_token) {
		case UNDECLARED:
			if (pass2)
				err[pflag]++;
			else {
				(yyvsp[0].itemptr)->i_token = LABEL;
				(yyvsp[0].itemptr)->i_value = dollarsign;
			}
			break;
		case LABEL:
			if (!pass2) {
				(yyvsp[0].itemptr)->i_token = MULTDEF;
				err[mflag]++;
			} else if ((yyvsp[0].itemptr)->i_value != dollarsign)
				err[pflag]++;
			break;
		default:
			err[mflag]++;
			(yyvsp[0].itemptr)->i_token = MULTDEF;
		}
	}
#line 3212 "y.tab.c"
    break;

  case 29: /* label.part: symbol maybecolon  */
#line 1283 "zmac.y"
                          {
		switch((yyvsp[-1].itemptr)->i_token) {
		case UNDECLARED:
			if (pass2)
				err[pflag]++;
			else {
				(yyvsp[-1].itemptr)->i_token = LABEL;
				(yyvsp[-1].itemptr)->i_value = dollarsign;
			}
			break;
		case LABEL:
			if (!pass2) {
				(yyvsp[-1].itemptr)->i_token = MULTDEF;
				err[mflag]++;
			} else if ((yyvsp[-1].itemptr)->i_value != dollarsign)
				err[pflag]++;
			break;
		default:
			err[mflag]++;
			(yyvsp[-1].itemptr)->i_token = MULTDEF;
		}
	}
#line 3239 "y.tab.c"
    break;

  case 30: /* operation: NOOPERAND  */
#line 1310 "zmac.y"
                { emit1((yyvsp[0].itemptr)->i_value, 0, 0, 1); }
#line 3245 "y.tab.c"
    break;

  case 31: /* operation: JP expression  */
#line 1313 "zmac.y"
                { emitjp(0303, (yyvsp[0].ival)); }
#line 3251 "y.tab.c"
    break;

  case 32: /* operation: CALL expression  */
#line 1316 "zmac.y"
                { emit(3, 0315, (yyvsp[0].ival), (yyvsp[0].ival) >> 8); }
#line 3257 "y.tab.c"
    break;

  case 33: /* operation: RST expression  */
#line 1319 "zmac.y"
                { int a = (yyvsp[0].ival), doneerr=0;
		/* added support for normal RST form -rjm */
		if (a >= 8) {
			if ((a&7)!=0) doneerr=1,err[vflag]++;
			a >>= 3;
		}
		if ((a > 7 || a < 0) && !doneerr) /* don't give two errs... */
			err[vflag]++;
		emit(1, (yyvsp[-1].itemptr)->i_value + (a << 3));
	}
#line 3272 "y.tab.c"
    break;

  case 34: /* operation: ADD expression  */
#line 1331 "zmac.y"
                { emit1(0306, 0, (yyvsp[0].ival), 3); if (pass2) warnprt (1, 0); }
#line 3278 "y.tab.c"
    break;

  case 35: /* operation: ADD ACC ',' expression  */
#line 1334 "zmac.y"
                { emit1(0306, 0, (yyvsp[0].ival), 3); }
#line 3284 "y.tab.c"
    break;

  case 36: /* operation: ARITHC expression  */
#line 1337 "zmac.y"
                { emit1(0306 + ((yyvsp[-1].itemptr)->i_value << 3), 0, (yyvsp[0].ival), 3); if (pass2) warnprt (1, 0); }
#line 3290 "y.tab.c"
    break;

  case 37: /* operation: ARITHC ACC ',' expression  */
#line 1340 "zmac.y"
                { emit1(0306 + ((yyvsp[-3].itemptr)->i_value << 3), 0, (yyvsp[0].ival), 3); }
#line 3296 "y.tab.c"
    break;

  case 38: /* operation: LOGICAL expression  */
#line 1343 "zmac.y"
                { emit1(0306 | ((yyvsp[-1].itemptr)->i_value << 3), 0, (yyvsp[0].ival), 3); }
#line 3302 "y.tab.c"
    break;

  case 39: /* operation: AND expression  */
#line 1346 "zmac.y"
                { emit1(0306 | ((yyvsp[-1].itemptr)->i_value << 3), 0, (yyvsp[0].ival), 3); }
#line 3308 "y.tab.c"
    break;

  case 40: /* operation: OR expression  */
#line 1349 "zmac.y"
                { emit1(0306 | ((yyvsp[-1].itemptr)->i_value << 3), 0, (yyvsp[0].ival), 3); }
#line 3314 "y.tab.c"
    break;

  case 41: /* operation: XOR expression  */
#line 1352 "zmac.y"
                { emit1(0306 | ((yyvsp[-1].itemptr)->i_value << 3), 0, (yyvsp[0].ival), 3); }
#line 3320 "y.tab.c"
    break;

  case 42: /* operation: LOGICAL ACC ',' expression  */
#line 1355 "zmac.y"
                { emit1(0306 | ((yyvsp[-3].itemptr)->i_value << 3), 0, (yyvsp[0].ival), 3); if (pass2) warnprt (1, 0); }
#line 3326 "y.tab.c"
    break;

  case 43: /* operation: AND ACC ',' expression  */
#line 1358 "zmac.y"
                { emit1(0306 | ((yyvsp[-3].itemptr)->i_value << 3), 0, (yyvsp[0].ival), 3); if (pass2) warnprt (1, 0); }
#line 3332 "y.tab.c"
    break;

  case 44: /* operation: OR ACC ',' expression  */
#line 1361 "zmac.y"
                { emit1(0306 | ((yyvsp[-3].itemptr)->i_value << 3), 0, (yyvsp[0].ival), 3); if (pass2) warnprt (1, 0); }
#line 3338 "y.tab.c"
    break;

  case 45: /* operation: XOR ACC ',' expression  */
#line 1364 "zmac.y"
                { emit1(0306 | ((yyvsp[-3].itemptr)->i_value << 3), 0, (yyvsp[0].ival), 3); if (pass2) warnprt (1, 0); }
#line 3344 "y.tab.c"
    break;

  case 46: /* operation: ADD reg  */
#line 1367 "zmac.y"
                { emit1(0200 + ((yyvsp[0].ival) & 0377), (yyvsp[0].ival), 0, 0); if (pass2) warnprt (1, 0); }
#line 3350 "y.tab.c"
    break;

  case 47: /* operation: ADD ACC ',' reg  */
#line 1370 "zmac.y"
                { emit1(0200 + ((yyvsp[0].ival) & 0377), (yyvsp[0].ival), 0, 0); }
#line 3356 "y.tab.c"
    break;

  case 48: /* operation: ARITHC reg  */
#line 1373 "zmac.y"
                { emit1(0200 + ((yyvsp[-1].itemptr)->i_value << 3) + ((yyvsp[0].ival) & 0377), (yyvsp[0].ival), 0, 0); if (pass2) warnprt (1, 0); }
#line 3362 "y.tab.c"
    break;

  case 49: /* operation: ARITHC ACC ',' reg  */
#line 1376 "zmac.y"
                { emit1(0200 + ((yyvsp[-3].itemptr)->i_value << 3) + ((yyvsp[0].ival) & 0377), (yyvsp[0].ival), 0, 0); }
#line 3368 "y.tab.c"
    break;

  case 50: /* operation: LOGICAL reg  */
#line 1379 "zmac.y"
                { emit1(0200 + ((yyvsp[-1].itemptr)->i_value << 3) + ((yyvsp[0].ival) & 0377), (yyvsp[0].ival), 0, 0); }
#line 3374 "y.tab.c"
    break;

  case 51: /* operation: AND reg  */
#line 1382 "zmac.y"
                { emit1(0200 + ((yyvsp[-1].itemptr)->i_value << 3) + ((yyvsp[0].ival) & 0377), (yyvsp[0].ival), 0, 0); }
#line 3380 "y.tab.c"
    break;

  case 52: /* operation: OR reg  */
#line 1385 "zmac.y"
                { emit1(0200 + ((yyvsp[-1].itemptr)->i_value << 3) + ((yyvsp[0].ival) & 0377), (yyvsp[0].ival), 0, 0); }
#line 3386 "y.tab.c"
    break;

  case 53: /* operation: XOR reg  */
#line 1388 "zmac.y"
                { emit1(0200 + ((yyvsp[-1].itemptr)->i_value << 3) + ((yyvsp[0].ival) & 0377), (yyvsp[0].ival), 0, 0); }
#line 3392 "y.tab.c"
    break;

  case 54: /* operation: LOGICAL ACC ',' reg  */
#line 1391 "zmac.y"
                { emit1(0200 + ((yyvsp[-3].itemptr)->i_value << 3) + ((yyvsp[0].ival) & 0377), (yyvsp[0].ival), 0, 0); if (pass2) warnprt (1, 0); }
#line 3398 "y.tab.c"
    break;

  case 55: /* operation: AND ACC ',' reg  */
#line 1394 "zmac.y"
                { emit1(0200 + ((yyvsp[-3].itemptr)->i_value << 3) + ((yyvsp[0].ival) & 0377), (yyvsp[0].ival), 0, 0); if (pass2) warnprt (1, 0); }
#line 3404 "y.tab.c"
    break;

  case 56: /* operation: OR ACC ',' reg  */
#line 1397 "zmac.y"
                { emit1(0200 + ((yyvsp[-3].itemptr)->i_value << 3) + ((yyvsp[0].ival) & 0377), (yyvsp[0].ival), 0, 0); if (pass2) warnprt (1, 0); }
#line 3410 "y.tab.c"
    break;

  case 57: /* operation: XOR ACC ',' reg  */
#line 1400 "zmac.y"
                { emit1(0200 + ((yyvsp[-3].itemptr)->i_value << 3) + ((yyvsp[0].ival) & 0377), (yyvsp[0].ival), 0, 0); if (pass2) warnprt (1, 0); }
#line 3416 "y.tab.c"
    break;

  case 58: /* operation: SHIFT reg  */
#line 1403 "zmac.y"
                {
			if (suggest_optimise && pass2 && ((yyvsp[0].ival) & 0377) == 7 && (yyvsp[-1].itemptr)->i_value <= 4)
				warnprt ((yyvsp[-1].itemptr)->i_value + 4, 0);
			if (pass2 && (yyvsp[-1].itemptr)->i_value == 6)
				warnprt (1, 0);
			emit1(0145400 + ((yyvsp[-1].itemptr)->i_value << 3) + ((yyvsp[0].ival) & 0377), (yyvsp[0].ival), 0, 0);
		}
#line 3428 "y.tab.c"
    break;

  case 59: /* operation: INCDEC reg  */
#line 1412 "zmac.y"
                { emit1((yyvsp[-1].itemptr)->i_value + (((yyvsp[0].ival) & 0377) << 3) + 4, (yyvsp[0].ival), 0, 0); }
#line 3434 "y.tab.c"
    break;

  case 60: /* operation: ARITHC HL ',' bcdehlsp  */
#line 1415 "zmac.y"
                { if ((yyvsp[-3].itemptr)->i_value == 1)
				emit(2,0355,0112+(yyvsp[0].ival));
			else
				emit(2,0355,0102+(yyvsp[0].ival));
		}
#line 3444 "y.tab.c"
    break;

  case 61: /* operation: ADD mar ',' bcdesp  */
#line 1422 "zmac.y"
                { emitdad((yyvsp[-2].ival),(yyvsp[0].ival)); }
#line 3450 "y.tab.c"
    break;

  case 62: /* operation: ADD mar ',' mar  */
#line 1425 "zmac.y"
                {
			if ((yyvsp[-2].ival) != (yyvsp[0].ival)) {
				fprintf(stderr,"ADD mar, mar error\n");
				err[fflag]++;
			}
			emitdad((yyvsp[-2].ival),(yyvsp[0].ival));
		}
#line 3462 "y.tab.c"
    break;

  case 63: /* operation: INCDEC evenreg  */
#line 1434 "zmac.y"
                { emit1(((yyvsp[-1].itemptr)->i_value << 3) + ((yyvsp[0].ival) & 0377) + 3, (yyvsp[0].ival), 0, 1); }
#line 3468 "y.tab.c"
    break;

  case 64: /* operation: PUSHPOP pushable  */
#line 1437 "zmac.y"
                { emit1((yyvsp[-1].itemptr)->i_value + ((yyvsp[0].ival) & 0377), (yyvsp[0].ival), 0, 1); }
#line 3474 "y.tab.c"
    break;

  case 65: /* operation: BIT expression ',' reg  */
#line 1440 "zmac.y"
                {
			if ((yyvsp[-2].ival) < 0 || (yyvsp[-2].ival) > 7)
				err[vflag]++;
			emit1((yyvsp[-3].itemptr)->i_value + (((yyvsp[-2].ival) & 7) << 3) + ((yyvsp[0].ival) & 0377), (yyvsp[0].ival), 0, 0);
		}
#line 3484 "y.tab.c"
    break;

  case 66: /* operation: JP condition ',' expression  */
#line 1447 "zmac.y"
                { emitjp(0302 + (yyvsp[-2].ival), (yyvsp[0].ival)); }
#line 3490 "y.tab.c"
    break;

  case 67: /* operation: JP '(' mar ')'  */
#line 1450 "zmac.y"
                { emit1(0351, (yyvsp[-1].ival), 0, 1); }
#line 3496 "y.tab.c"
    break;

  case 68: /* operation: JP mar  */
#line 1453 "zmac.y"
                { emit1(0351, (yyvsp[0].ival), 0, 1); if (pass2) warnprt (1, 0); }
#line 3502 "y.tab.c"
    break;

  case 69: /* operation: CALL condition ',' expression  */
#line 1456 "zmac.y"
                { emit(3, 0304 + (yyvsp[-2].ival), (yyvsp[0].ival), (yyvsp[0].ival) >> 8); }
#line 3508 "y.tab.c"
    break;

  case 70: /* operation: JR expression  */
#line 1459 "zmac.y"
                { emitjr(030,(yyvsp[0].ival)); }
#line 3514 "y.tab.c"
    break;

  case 71: /* operation: JR spcondition ',' expression  */
#line 1462 "zmac.y"
                { emitjr((yyvsp[-3].itemptr)->i_value + (yyvsp[-2].ival), (yyvsp[0].ival)); }
#line 3520 "y.tab.c"
    break;

  case 72: /* operation: DJNZ expression  */
#line 1465 "zmac.y"
                { emitjr((yyvsp[-1].itemptr)->i_value, (yyvsp[0].ival)); }
#line 3526 "y.tab.c"
    break;

  case 73: /* operation: RET  */
#line 1468 "zmac.y"
                { emit(1, (yyvsp[0].itemptr)->i_value); }
#line 3532 "y.tab.c"
    break;

  case 74: /* operation: RET condition  */
#line 1471 "zmac.y"
                { emit(1, 0300 + (yyvsp[0].ival)); }
#line 3538 "y.tab.c"
    break;

  case 75: /* operation: LD reg ',' reg  */
#line 1474 "zmac.y"
                {
			if (((yyvsp[-2].ival) & 0377) == 6 && ((yyvsp[0].ival) & 0377) == 6) {
				fprintf(stderr,"LD reg, reg error\n");
				err[fflag]++;
			}
			emit1(0100 + (((yyvsp[-2].ival) & 7) << 3) + ((yyvsp[0].ival) & 7),(yyvsp[-2].ival) | (yyvsp[0].ival), 0, 0);
		}
#line 3550 "y.tab.c"
    break;

  case 76: /* operation: LD reg ',' noparenexpr  */
#line 1483 "zmac.y"
                {
			if (suggest_optimise && pass2 && (yyvsp[0].ival) == 0 && ((yyvsp[-2].ival) & 0377) == 7)
				warnprt (3, 0);
			emit1(6 + (((yyvsp[-2].ival) & 0377) << 3), (yyvsp[-2].ival), (yyvsp[0].ival), 2);
		}
#line 3560 "y.tab.c"
    break;

  case 77: /* operation: LD reg ',' '(' RP ')'  */
#line 1490 "zmac.y"
                {	if ((yyvsp[-4].ival) != 7) {
				fprintf(stderr,"LD reg, (RP) error\n");
				err[fflag]++;
			}
			else emit(1, 012 + (yyvsp[-1].itemptr)->i_value);
		}
#line 3571 "y.tab.c"
    break;

  case 78: /* operation: LD reg ',' parenexpr  */
#line 1498 "zmac.y"
                {
			if ((yyvsp[-2].ival) != 7) {
				fprintf(stderr,"LD reg, (expr) error\n");
				err[fflag]++;
			}
			else emit(3, 072, (yyvsp[0].ival), (yyvsp[0].ival) >> 8);
		}
#line 3583 "y.tab.c"
    break;

  case 79: /* operation: LD '(' RP ')' ',' ACC  */
#line 1507 "zmac.y"
                { emit(1, 2 + (yyvsp[-3].itemptr)->i_value); }
#line 3589 "y.tab.c"
    break;

  case 80: /* operation: LD parenexpr ',' ACC  */
#line 1510 "zmac.y"
                { emit(3, 062, (yyvsp[-2].ival), (yyvsp[-2].ival) >> 8); }
#line 3595 "y.tab.c"
    break;

  case 81: /* operation: LD reg ',' MISCREG  */
#line 1513 "zmac.y"
                {
			if ((yyvsp[-2].ival) != 7) {
				fprintf(stderr,"LD reg, MISCREG error\n");
				err[fflag]++;
			}
			else emit(2, 0355, 0127 + (yyvsp[0].itemptr)->i_value);
		}
#line 3607 "y.tab.c"
    break;

  case 82: /* operation: LD MISCREG ',' ACC  */
#line 1522 "zmac.y"
                { emit(2, 0355, 0107 + (yyvsp[-2].itemptr)->i_value); }
#line 3613 "y.tab.c"
    break;

  case 83: /* operation: LD evenreg ',' lxexpression  */
#line 1525 "zmac.y"
                { emit1(1 + ((yyvsp[-2].ival) & 060), (yyvsp[-2].ival), (yyvsp[0].ival), 5); }
#line 3619 "y.tab.c"
    break;

  case 84: /* operation: LD evenreg ',' parenexpr  */
#line 1528 "zmac.y"
                {
			if (((yyvsp[-2].ival) & 060) == 040)
				emit1(052, (yyvsp[-2].ival), (yyvsp[0].ival), 5);
			else
				emit(4, 0355, 0113 + (yyvsp[-2].ival), (yyvsp[0].ival), (yyvsp[0].ival) >> 8);
		}
#line 3630 "y.tab.c"
    break;

  case 85: /* operation: LD parenexpr ',' evenreg  */
#line 1536 "zmac.y"
                {
			if (((yyvsp[0].ival) & 060) == 040)
				emit1(042, (yyvsp[0].ival), (yyvsp[-2].ival), 5);
			else
				emit(4, 0355, 0103 + (yyvsp[0].ival), (yyvsp[-2].ival), (yyvsp[-2].ival) >> 8);
		}
#line 3641 "y.tab.c"
    break;

  case 86: /* operation: LD evenreg ',' mar  */
#line 1544 "zmac.y"
                {
			if ((yyvsp[-2].ival) != 060) {
				fprintf(stderr,"LD evenreg error\n");
				err[fflag]++;
			}
			else
				emit1(0371, (yyvsp[0].ival), 0, 1);
		}
#line 3654 "y.tab.c"
    break;

  case 87: /* operation: EX RP ',' HL  */
#line 1554 "zmac.y"
                {
			if ((yyvsp[-2].itemptr)->i_value != 020) {
				fprintf(stderr,"EX RP, HL error\n");
				err[fflag]++;
			}
			else
				emit(1, 0353);
		}
#line 3667 "y.tab.c"
    break;

  case 88: /* operation: EX AF ',' AF setqf '\'' clrqf  */
#line 1564 "zmac.y"
                { emit(1, 010); }
#line 3673 "y.tab.c"
    break;

  case 89: /* operation: EX '(' SP ')' ',' mar  */
#line 1567 "zmac.y"
                { emit1(0343, (yyvsp[0].ival), 0, 1); }
#line 3679 "y.tab.c"
    break;

  case 90: /* operation: IN realreg ',' parenexpr  */
#line 1570 "zmac.y"
                {
			if ((yyvsp[-2].ival) != 7) {
				fprintf(stderr,"IN reg, (expr) error\n");
				err[fflag]++;
			}
			else	{
				if ((yyvsp[0].ival) < 0 || (yyvsp[0].ival) > 255)
					err[vflag]++;
				emit(2, (yyvsp[-3].itemptr)->i_value, (yyvsp[0].ival));
			}
		}
#line 3695 "y.tab.c"
    break;

  case 91: /* operation: IN realreg ',' '(' C ')'  */
#line 1583 "zmac.y"
                { emit(2, 0355, 0100 + ((yyvsp[-4].ival) << 3)); }
#line 3701 "y.tab.c"
    break;

  case 92: /* operation: IN F ',' '(' C ')'  */
#line 1586 "zmac.y"
                { emit(2, 0355, 0160); }
#line 3707 "y.tab.c"
    break;

  case 93: /* operation: OUT parenexpr ',' ACC  */
#line 1589 "zmac.y"
                {
			if ((yyvsp[-2].ival) < 0 || (yyvsp[-2].ival) > 255)
				err[vflag]++;
			emit(2, (yyvsp[-3].itemptr)->i_value, (yyvsp[-2].ival));
		}
#line 3717 "y.tab.c"
    break;

  case 94: /* operation: OUT '(' C ')' ',' realreg  */
#line 1596 "zmac.y"
                { emit(2, 0355, 0101 + ((yyvsp[0].ival) << 3)); }
#line 3723 "y.tab.c"
    break;

  case 95: /* operation: IM expression  */
#line 1599 "zmac.y"
                {
			if ((yyvsp[0].ival) > 2 || (yyvsp[0].ival) < 0)
				err[vflag]++;
			else
				emit(2, (yyvsp[-1].itemptr)->i_value >> 8, (yyvsp[-1].itemptr)->i_value + (((yyvsp[0].ival) + ((yyvsp[0].ival) > 0)) << 3));
		}
#line 3734 "y.tab.c"
    break;

  case 96: /* operation: PHASE expression  */
#line 1607 "zmac.y"
                {
			if (phaseflag) {
				err[oflag]++;
			} else {
				phaseflag = 1;
				phdollar = dollarsign;
				dollarsign = (yyvsp[0].ival);
				phbegin = dollarsign;
			}
		}
#line 3749 "y.tab.c"
    break;

  case 97: /* operation: DEPHASE  */
#line 1619 "zmac.y"
                {
			if (!phaseflag) {
				err[oflag]++;
			} else {
				phaseflag = 0;
				dollarsign = phdollar + dollarsign - phbegin;
			}
		}
#line 3762 "y.tab.c"
    break;

  case 98: /* operation: ORG expression  */
#line 1629 "zmac.y"
                {
			if (not_seen_org)
				first_org_store=yyvsp[0].ival;
			not_seen_org=0;
			if (phaseflag) {
				err[oflag]++;
				dollarsign = phdollar + dollarsign - phbegin;
				phaseflag = 0;
			}
			if ((yyvsp[0].ival)-dollarsign) {
				flushbin();
				if (pass2 && !output_hex && dollarsign != 0) {
					if (yyvsp[0].ival < dollarsign) {
						err[orgflag]++;
					} else {
						int f;
						for (f=0;f<(yyvsp[0].ival - dollarsign);f++)
							fputc(0, fbuf);
					}
				}
				olddollar = (yyvsp[0].ival);
				dollarsign = (yyvsp[0].ival);
			}
		}
#line 3791 "y.tab.c"
    break;

  case 105: /* parm.element: UNDECLARED  */
#line 1672 "zmac.y"
                {
			(yyvsp[0].itemptr)->i_token = MPARM;
			if (parm_number >= PARMMAX)
				error("Too many parameters");
			(yyvsp[0].itemptr)->i_value = parm_number++;
		}
#line 3802 "y.tab.c"
    break;

  case 109: /* arg.element: ARG  */
#line 1692 "zmac.y"
                {
			cp = malloc(strlen(tempbuf)+1);
#ifdef M_DEBUG
			fprintf (stderr, "[Arg%u(%p): %s]\n", parm_number, cp, tempbuf);
#endif
			est2[parm_number++] = cp;
			strcpy(cp, tempbuf);
		}
#line 3815 "y.tab.c"
    break;

  case 112: /* realreg: REGNAME  */
#line 1708 "zmac.y"
                {
			(yyval.ival) = (yyvsp[0].itemptr)->i_value;
		}
#line 3823 "y.tab.c"
    break;

  case 113: /* realreg: ACC  */
#line 1713 "zmac.y"
                {
			(yyval.ival) = (yyvsp[0].itemptr)->i_value;
		}
#line 3831 "y.tab.c"
    break;

  case 114: /* realreg: C  */
#line 1718 "zmac.y"
                {
			(yyval.ival) = (yyvsp[0].itemptr)->i_value;
		}
#line 3839 "y.tab.c"
    break;

  case 115: /* mem: '(' HL ')'  */
#line 1724 "zmac.y"
                {
			(yyval.ival) = 6;
		}
#line 3847 "y.tab.c"
    break;

  case 116: /* mem: '(' INDEX expression ')'  */
#line 1729 "zmac.y"
                {
			disp = (yyvsp[-1].ival);
			(yyval.ival) = ((yyvsp[-2].itemptr)->i_value & 0177400) | 6;
		}
#line 3856 "y.tab.c"
    break;

  case 117: /* mem: '(' INDEX ')'  */
#line 1735 "zmac.y"
                {
			disp = 0;
			(yyval.ival) = ((yyvsp[-1].itemptr)->i_value & 0177400) | 6;
		}
#line 3865 "y.tab.c"
    break;

  case 120: /* pushable: RP  */
#line 1747 "zmac.y"
                {
			(yyval.ival) = (yyvsp[0].itemptr)->i_value;
		}
#line 3873 "y.tab.c"
    break;

  case 121: /* pushable: AF  */
#line 1752 "zmac.y"
                {
			(yyval.ival) = (yyvsp[0].itemptr)->i_value;
		}
#line 3881 "y.tab.c"
    break;

  case 123: /* bcdesp: RP  */
#line 1760 "zmac.y"
                {
			(yyval.ival) = (yyvsp[0].itemptr)->i_value;
		}
#line 3889 "y.tab.c"
    break;

  case 124: /* bcdesp: SP  */
#line 1765 "zmac.y"
                {
			(yyval.ival) = (yyvsp[0].itemptr)->i_value;
		}
#line 3897 "y.tab.c"
    break;

  case 126: /* bcdehlsp: HL  */
#line 1773 "zmac.y"
                {
			(yyval.ival) = (yyvsp[0].itemptr)->i_value;
		}
#line 3905 "y.tab.c"
    break;

  case 127: /* mar: HL  */
#line 1779 "zmac.y"
                {
			(yyval.ival) = (yyvsp[0].itemptr)->i_value;
		}
#line 3913 "y.tab.c"
    break;

  case 128: /* mar: INDEX  */
#line 1784 "zmac.y"
                {
			(yyval.ival) = (yyvsp[0].itemptr)->i_value;
		}
#line 3921 "y.tab.c"
    break;

  case 130: /* condition: COND  */
#line 1792 "zmac.y"
                {
			(yyval.ival) = (yyvsp[0].itemptr)->i_value;
		}
#line 3929 "y.tab.c"
    break;

  case 131: /* spcondition: SPCOND  */
#line 1798 "zmac.y"
                {
			(yyval.ival) = (yyvsp[0].itemptr)->i_value;
		}
#line 3937 "y.tab.c"
    break;

  case 132: /* spcondition: C  */
#line 1803 "zmac.y"
                {	(yyval.ival) = 030;	}
#line 3943 "y.tab.c"
    break;

  case 135: /* db.list.element: TWOCHAR  */
#line 1812 "zmac.y"
                {
			dataemit(2, (yyvsp[0].ival), (yyvsp[0].ival)>>8);
		}
#line 3951 "y.tab.c"
    break;

  case 136: /* db.list.element: STRING  */
#line 1817 "zmac.y"
                {
			cp = (yyvsp[0].cval);
			while (*cp != '\0')
				dataemit(1,*cp++);
		}
#line 3961 "y.tab.c"
    break;

  case 137: /* db.list.element: expression  */
#line 1824 "zmac.y"
                {
			if ((yyvsp[0].ival) < -128 || (yyvsp[0].ival) > 255)
					err[vflag]++;
			dataemit(1, (yyvsp[0].ival) & 0377);
		}
#line 3971 "y.tab.c"
    break;

  case 140: /* dw.list.element: expression  */
#line 1841 "zmac.y"
                {
			dataemit(2, (yyvsp[0].ival), (yyvsp[0].ival)>>8);
		}
#line 3979 "y.tab.c"
    break;

  case 145: /* parenexpr: '(' expression ')'  */
#line 1862 "zmac.y"
                {	(yyval.ival) = (yyvsp[-1].ival);	}
#line 3985 "y.tab.c"
    break;

  case 146: /* noparenexpr: LABEL  */
#line 1867 "zmac.y"
                {	(yyval.ival) = (yyvsp[0].itemptr)->i_value; (yyvsp[0].itemptr)->i_uses++ ;	}
#line 3991 "y.tab.c"
    break;

  case 149: /* noparenexpr: EQUATED  */
#line 1874 "zmac.y"
                {	(yyval.ival) = (yyvsp[0].itemptr)->i_value; (yyvsp[0].itemptr)->i_uses++ ;	}
#line 3997 "y.tab.c"
    break;

  case 150: /* noparenexpr: WASEQUATED  */
#line 1877 "zmac.y"
                {
			(yyval.ival) = (yyvsp[0].itemptr)->i_value; (yyvsp[0].itemptr)->i_uses++ ;
			if ((yyvsp[0].itemptr)->i_equbad) {
				/* forward reference to equ with a forward
				 * reference of its own cannot be resolved
				 * in two passes. -rjm
				 */
				err[frflag]++;
			}
		}
#line 4012 "y.tab.c"
    break;

  case 151: /* noparenexpr: DEFLED  */
#line 1889 "zmac.y"
                {	(yyval.ival) = (yyvsp[0].itemptr)->i_value; (yyvsp[0].itemptr)->i_uses++ ;	}
#line 4018 "y.tab.c"
    break;

  case 152: /* noparenexpr: '$'  */
#line 1892 "zmac.y"
                {	(yyval.ival) = dollarsign;	}
#line 4024 "y.tab.c"
    break;

  case 153: /* noparenexpr: UNDECLARED  */
#line 1895 "zmac.y"
                {
			err[uflag]++;
			equ_bad_label=1;
			(yyval.ival) = 0;
		}
#line 4034 "y.tab.c"
    break;

  case 154: /* noparenexpr: MULTDEF  */
#line 1902 "zmac.y"
                {	(yyval.ival) = (yyvsp[0].itemptr)->i_value;	}
#line 4040 "y.tab.c"
    break;

  case 155: /* noparenexpr: expression '+' expression  */
#line 1905 "zmac.y"
                {	(yyval.ival) = (yyvsp[-2].ival) + (yyvsp[0].ival);	}
#line 4046 "y.tab.c"
    break;

  case 156: /* noparenexpr: expression '-' expression  */
#line 1908 "zmac.y"
                {	(yyval.ival) = (yyvsp[-2].ival) - (yyvsp[0].ival);	}
#line 4052 "y.tab.c"
    break;

  case 157: /* noparenexpr: expression '/' expression  */
#line 1911 "zmac.y"
                {	if ((yyvsp[0].ival) == 0) err[eflag]++; else (yyval.ival) = (yyvsp[-2].ival) / (yyvsp[0].ival);	}
#line 4058 "y.tab.c"
    break;

  case 158: /* noparenexpr: expression '*' expression  */
#line 1914 "zmac.y"
                {	(yyval.ival) = (yyvsp[-2].ival) * (yyvsp[0].ival);	}
#line 4064 "y.tab.c"
    break;

  case 159: /* noparenexpr: expression '%' expression  */
#line 1917 "zmac.y"
                {	if ((yyvsp[0].ival) == 0) err[eflag]++; else (yyval.ival) = (yyvsp[-2].ival) % (yyvsp[0].ival);	}
#line 4070 "y.tab.c"
    break;

  case 160: /* noparenexpr: expression MOD expression  */
#line 1920 "zmac.y"
                {	if ((yyvsp[0].ival) == 0) err[eflag]++; else (yyval.ival) = (yyvsp[-2].ival) % (yyvsp[0].ival);	}
#line 4076 "y.tab.c"
    break;

  case 161: /* noparenexpr: expression '&' expression  */
#line 1923 "zmac.y"
                {	(yyval.ival) = (yyvsp[-2].ival) & (yyvsp[0].ival);	}
#line 4082 "y.tab.c"
    break;

  case 162: /* noparenexpr: expression AND expression  */
#line 1926 "zmac.y"
                {	(yyval.ival) = (yyvsp[-2].ival) & (yyvsp[0].ival);	}
#line 4088 "y.tab.c"
    break;

  case 163: /* noparenexpr: expression '|' expression  */
#line 1929 "zmac.y"
                {	(yyval.ival) = (yyvsp[-2].ival) | (yyvsp[0].ival);	}
#line 4094 "y.tab.c"
    break;

  case 164: /* noparenexpr: expression OR expression  */
#line 1932 "zmac.y"
                {	(yyval.ival) = (yyvsp[-2].ival) | (yyvsp[0].ival);	}
#line 4100 "y.tab.c"
    break;

  case 165: /* noparenexpr: expression '^' expression  */
#line 1935 "zmac.y"
                {	(yyval.ival) = (yyvsp[-2].ival) ^ (yyvsp[0].ival);	}
#line 4106 "y.tab.c"
    break;

  case 166: /* noparenexpr: expression XOR expression  */
#line 1938 "zmac.y"
                {	(yyval.ival) = (yyvsp[-2].ival) ^ (yyvsp[0].ival);	}
#line 4112 "y.tab.c"
    break;

  case 167: /* noparenexpr: expression SHL expression  */
#line 1941 "zmac.y"
                {	(yyval.ival) = (yyvsp[-2].ival) << (yyvsp[0].ival);	}
#line 4118 "y.tab.c"
    break;

  case 168: /* noparenexpr: expression SHR expression  */
#line 1944 "zmac.y"
                {	(yyval.ival) = (((yyvsp[-2].ival) >> 1) & 077777) >> ((yyvsp[0].ival) - 1);	}
#line 4124 "y.tab.c"
    break;

  case 169: /* noparenexpr: expression '<' expression  */
#line 1947 "zmac.y"
                {	(yyval.ival) = (yyvsp[-2].ival) < (yyvsp[0].ival);	}
#line 4130 "y.tab.c"
    break;

  case 170: /* noparenexpr: expression '=' expression  */
#line 1950 "zmac.y"
                {	(yyval.ival) = (yyvsp[-2].ival) == (yyvsp[0].ival);	}
#line 4136 "y.tab.c"
    break;

  case 171: /* noparenexpr: expression '>' expression  */
#line 1953 "zmac.y"
                {	(yyval.ival) = (yyvsp[-2].ival) > (yyvsp[0].ival);	}
#line 4142 "y.tab.c"
    break;

  case 172: /* noparenexpr: expression LT expression  */
#line 1956 "zmac.y"
                {	(yyval.ival) = (yyvsp[-2].ival) < (yyvsp[0].ival);	}
#line 4148 "y.tab.c"
    break;

  case 173: /* noparenexpr: expression EQ expression  */
#line 1959 "zmac.y"
                {	(yyval.ival) = (yyvsp[-2].ival) == (yyvsp[0].ival);	}
#line 4154 "y.tab.c"
    break;

  case 174: /* noparenexpr: expression GT expression  */
#line 1962 "zmac.y"
                {	(yyval.ival) = (yyvsp[-2].ival) > (yyvsp[0].ival);	}
#line 4160 "y.tab.c"
    break;

  case 175: /* noparenexpr: expression LE expression  */
#line 1965 "zmac.y"
                {	(yyval.ival) = (yyvsp[-2].ival) <= (yyvsp[0].ival);	}
#line 4166 "y.tab.c"
    break;

  case 176: /* noparenexpr: expression GE expression  */
#line 1968 "zmac.y"
                {	(yyval.ival) = (yyvsp[-2].ival) >= (yyvsp[0].ival);	}
#line 4172 "y.tab.c"
    break;

  case 177: /* noparenexpr: expression NE expression  */
#line 1971 "zmac.y"
                {	(yyval.ival) = (yyvsp[-2].ival) != (yyvsp[0].ival);	}
#line 4178 "y.tab.c"
    break;

  case 178: /* noparenexpr: '[' expression ']'  */
#line 1974 "zmac.y"
                {	(yyval.ival) = (yyvsp[-1].ival);	}
#line 4184 "y.tab.c"
    break;

  case 179: /* noparenexpr: NOT expression  */
#line 1977 "zmac.y"
                {	(yyval.ival) = ~(yyvsp[0].ival);	}
#line 4190 "y.tab.c"
    break;

  case 180: /* noparenexpr: '~' expression  */
#line 1980 "zmac.y"
                {	(yyval.ival) = ~(yyvsp[0].ival);	}
#line 4196 "y.tab.c"
    break;

  case 181: /* noparenexpr: '!' expression  */
#line 1983 "zmac.y"
                {	(yyval.ival) = !(yyvsp[0].ival);	}
#line 4202 "y.tab.c"
    break;

  case 182: /* noparenexpr: '+' expression  */
#line 1986 "zmac.y"
                {	(yyval.ival) = (yyvsp[0].ival);	}
#line 4208 "y.tab.c"
    break;

  case 183: /* noparenexpr: '-' expression  */
#line 1989 "zmac.y"
                {	(yyval.ival) = -(yyvsp[0].ival);	}
#line 4214 "y.tab.c"
    break;

  case 190: /* al: %empty  */
#line 2008 "zmac.y"
        { int i;
		if (expptr >= MAXEXP)
			error("Macro expansion level");
		est2 = (char **) malloc((PARMMAX +4) * sizeof(char *));
		expstack[expptr] = (char *)est2 ;
		for (i=0; i<PARMMAX; i++)
			est2[i] = 0;
		arg_flag++;
	}
#line 4228 "y.tab.c"
    break;

  case 191: /* arg_on: %empty  */
#line 2021 "zmac.y"
        {	arg_flag++;	}
#line 4234 "y.tab.c"
    break;

  case 192: /* arg_off: %empty  */
#line 2025 "zmac.y"
                {	arg_flag = 0;	}
#line 4240 "y.tab.c"
    break;

  case 193: /* setqf: %empty  */
#line 2029 "zmac.y"
                {	quoteflag++;	}
#line 4246 "y.tab.c"
    break;

  case 194: /* clrqf: %empty  */
#line 2033 "zmac.y"
                {	quoteflag = 0;	}
#line 4252 "y.tab.c"
    break;


#line 4256 "y.tab.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      yyerror (YY_("syntax error"));
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}

#line 2037 "zmac.y"


#define F_END	0
#define OTHER	1
#define SPACE	2
#define DIGIT	3
#define LETTER	4
#define STARTER 5
#define HEXIN	6


/*
 *  This is the table of character classes.  It is used by the lexical
 *  analyser. (yylex())
 */
char	charclass[] = {
	F_END,	OTHER,	OTHER,	OTHER,	OTHER,	OTHER,	OTHER,	OTHER,
	OTHER,	SPACE,	OTHER,	OTHER,	OTHER,	SPACE,	OTHER,	OTHER,
	OTHER,	OTHER,	OTHER,	OTHER,	OTHER,	OTHER,	OTHER,	OTHER,
	OTHER,	OTHER,	OTHER,	OTHER,	OTHER,	OTHER,	OTHER,	OTHER,
	SPACE,	OTHER,	OTHER,	HEXIN,	HEXIN,	OTHER,	HEXIN,	OTHER,
	OTHER,	OTHER,	OTHER,	OTHER,	OTHER,	OTHER,	OTHER,	OTHER,
	DIGIT,	DIGIT,	DIGIT,	DIGIT,	DIGIT,	DIGIT,	DIGIT,	DIGIT,
	DIGIT,	DIGIT,	OTHER,	OTHER,	OTHER,	OTHER,	OTHER,	STARTER,
	STARTER,LETTER, LETTER, LETTER, LETTER, LETTER, LETTER, LETTER,
	LETTER, LETTER, LETTER, LETTER, LETTER, LETTER, LETTER, LETTER,
	LETTER, LETTER, LETTER, LETTER, LETTER, LETTER, LETTER, LETTER,
	LETTER, LETTER, LETTER, OTHER,	OTHER,	OTHER,	OTHER,	LETTER,
	OTHER,	LETTER, LETTER, LETTER, LETTER, LETTER, LETTER, LETTER,
	LETTER, LETTER, LETTER, LETTER, LETTER, LETTER, LETTER, LETTER,
	LETTER, LETTER, LETTER, LETTER, LETTER, LETTER, LETTER, LETTER,
	LETTER, LETTER, LETTER, OTHER,	OTHER,	OTHER,	OTHER,	OTHER,
};


/*
 *  the following table tells which characters are parts of numbers.
 *  The entry is non-zero for characters which can be parts of numbers.
 */
char	numpart[] = {
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	'0',	'1',	'2',	'3',	'4',	'5',	'6',	'7',
	'8',	'9',	0,	0,	0,	0,	0,	0,
	0,	'A',	'B',	'C',	'D',	'E',	'F',	0,
	'H',	0,	0,	0,	0,	0,	0,	'O',
	0,	'Q',	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0,	'a',	'b',	'c',	'd',	'e',	'f',	0,
	'h',	0,	0,	0,	0,	0,	0,	'o',
	0,	'q',	0,	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,	0,	0,	0,
	0};




/*
 *  the following table is a list of assembler mnemonics;
 *  for each mnemonic the associated machine-code bit pattern
 *  and symbol type are given.
 */
struct	item	keytab[] = {
	{"a",	7,	ACC,		0},
	{"adc",	1,	ARITHC,		0},
	{"add",	0,	ADD,		0},
	{"af",	060,	AF,		0},
	{"and",	4,	AND,		0},
	{"ascii",0,	DEFB,		0},
	{"b",	0,	REGNAME,	0},
	{"bc",	0,	RP,		0},
	{"bit",	0145500,BIT,		0},
	{"block",0,	DEFS,		0},
	{"byte",0,	DEFB,		0},
	{"c",	1,	C,		0},
	{"call",0315,	CALL,		0},
	{"ccf",	077,	NOOPERAND,	0},
	{"cmp",	7,	LOGICAL,	0},		/* -cdk */
	{"cond",0,	IF,		0},
	{"cp",	7,	LOGICAL,	0},
	{"cpd",	0166651,NOOPERAND,	0},
	{"cpdr",0166671,NOOPERAND,	0},
	{"cpi",	0166641,NOOPERAND,	0},
	{"cpir",0166661,NOOPERAND,	0},
	{"cpl",	057,	NOOPERAND,	0},
	{"d",	2,	REGNAME,	0},
	{"daa",	0047,	NOOPERAND,	0},
	{"db",	0,	DEFB,		0},
	{"de",	020,	RP,		0},
	{"dec",	1,	INCDEC,		0},
	{"defb",0,	DEFB,		0},
	{"defl",0,	DEFL,		0},
	{"defm",0,	DEFB,		0},
	{"defs",0,	DEFS,		0},
	{"defw",0,	DEFW,		0},
	{"dephase",0,	DEPHASE,	0},
	{"di",	0363,	NOOPERAND,	0},
	{"djnz",020,	DJNZ,		0},
	{"ds",	0,	DEFS,		0},
	{"dw",	0,	DEFW,		0},
	{"e",	3,	REGNAME,	0},
	{"ei",	0373,	NOOPERAND,	0},
	{"eject",1,	LIST,		0},
	{"elist",3,	LIST,		0},
	{"else",0,	ELSE,		0},
	{"end",	0,	END,		0},
	{"endc",0,	ENDIF,		0},
	{"endif",0,	ENDIF,		0},
	{"endm", 0,	ENDM,		0},
	{"eq",	0,	EQ,		0},
	{"equ",	0,	EQU,		0},
	{"ex",	0,	EX,		0},
	{"exx",	0331,	NOOPERAND,	0},
	{"f",	0,	F,		0},
	{"flist",4,	LIST,		0},
	{"ge",	0,	GE,		0},
	{"glist",5,	LIST,		0},
	{"gt",	0,	GT,		0},
	{"h",	4,	REGNAME,	0},
	{"halt",0166,	NOOPERAND,	0},
	{"hl",	040,	HL,		0},
	{"i",	0,	MISCREG,	0},
	{"if",	0,	IF,		0},
	{"im",	0166506,IM,		0},
	{"in",	0333,	IN,		0},
	{"inc",	0,	INCDEC,		0},
	{"include", 3,	ARGPSEUDO,	0},
	{"ind",	0166652,NOOPERAND,	0},
	{"indr",0166672,NOOPERAND,	0},
	{"ini",	0166642,NOOPERAND,	0},
	{"inir",0166662,NOOPERAND,	0},
	{"ix",	0156440,INDEX,		0},
	{"iy",	0176440,INDEX,		0},
	{"jmp",	0303,	JP,		0},		/* -cdk */
	{"jp",	0303,	JP,		0},
	{"jr",	040,	JR,		0},
	{"l",	5,	REGNAME,	0},
	{"ld",	0,	LD,		0},
	{"ldd",	0166650,NOOPERAND,	0},
	{"lddr",0166670,NOOPERAND,	0},
	{"ldi",	0166640,NOOPERAND,	0},
	{"ldir",0166660,NOOPERAND,	0},
	{"le",	0,	LE,		0},
	{"list",0,	LIST,		0},
	{"lt",	0,	LT,		0},
	{"m",	070,	COND,		0},
	{"macro",0,	MACRO,		0},
	{"max",	1,	MINMAX,		0},
	{"min",	0,	MINMAX,		0},
	{"mlist",6,	LIST,		0},
	{"mod",	0,	MOD,		0},
	{"nc",	020,	SPCOND,		0},
	{"ne",	0,	NE,		0},
	{"neg",	0166504,NOOPERAND,	0},
	{"nolist",-1,	LIST,		0},
	{"nop",	0,	NOOPERAND,	0},
	{"not",	0,	NOT,		0},
	{"nv",	040,	COND,		0},
	{"nz",	0,	SPCOND,		0},
	{"or",	6,	OR,		0},
	{"org",	0,	ORG,		0},
	{"otdr",0166673,NOOPERAND,	0},
	{"otir",0166663,NOOPERAND,	0},
	{"out",	0323,	OUT,		0},
	{"outd",0166653,NOOPERAND,	0},
	{"outi",0166643,NOOPERAND,	0},
	{"p",	060,	COND,		0},
	{"pe",	050,	COND,		0},
	{"phase",0,	PHASE,		0},
	{"po",	040,	COND,		0},
	{"pop",	0301,	PUSHPOP,	0},
	{"push", 0305,	PUSHPOP,	0},
	{"r",	010,	MISCREG,	0},
	{"read", 3,	ARGPSEUDO,	0},
	{"res",	0145600,BIT,		0},
	{"ret",	0311,	RET,		0},
	{"reti",0166515,NOOPERAND,	0},
	{"retn",0166505,NOOPERAND,	0},
	{"rl",	2,	SHIFT,		0},
	{"rla",	027,	NOOPERAND,	0},
	{"rlc",	0,	SHIFT,		0},
	{"rlca",07,	NOOPERAND,	0},
	{"rld",	0166557,NOOPERAND,	0},
	{"rmem",0,	DEFS,		0},
	{"rr",	3,	SHIFT,		0},
	{"rra",	037,	NOOPERAND,	0},
	{"rrc",	1,	SHIFT,		0},
	{"rrca",017,	NOOPERAND,	0},
	{"rrd",	0166547,NOOPERAND,	0},
	{"rst",	0307,	RST,		0},
	{"rsym",1,	ARGPSEUDO,	0},
	{"sbc",	3,	ARITHC,		0},
	{"scf",	067,	NOOPERAND,	0},
	{"set",	0145700,BIT,		0},
	{"shl",	0,	SHL,		0},
	{"shr",	0,	SHR,		0},
	{"sla",	4,	SHIFT,		0},
	{"sll",	6,	SHIFT,		0}, /* Undocumented */
	{"sp",	060,	SP,		0},
	{"space",2,	LIST,		0},
	{"sra",	5,	SHIFT,		0},
	{"srl",	7,	SHIFT,		0},
	{"sub",	2,	LOGICAL,	0},
	{"text",0,	DEFB,		0},
	{"title",0,	ARGPSEUDO,	0},
	{"v",	050,	COND,		0},
	{"word",0,	DEFW,		0},
	{"wsym",2,	ARGPSEUDO,	0},
	{"xor",	5,	XOR,		0},
	{"z",	010,	SPCOND,		0}
};

/*
 *  user-defined items are tabulated in the following table.
 */

struct item	itemtab[ITEMTABLESIZE];
struct item	*itemmax = itemtab+ITEMTABLESIZE;





/*
 *  lexical analyser, called by yyparse.
 */
int yylex()
{
	int	c;
	char *p;
	int	radix;
	int  limit;
	int leadinghex = 0;

	if (arg_flag)
		return(getarg());
loop switch(charclass[c = nextchar()]) {
	case F_END:
		if (expptr) {
			popsi();
			continue;
		} else return(0);

	case SPACE:
		break;
	case LETTER:
	case STARTER:
		p = tempbuf;
		do {
			if (p >= tempmax)
				error(symlong);
			*p++ = (c >= 'A' && c <= 'Z') ? c + 'a' - 'A' : c;
			while	((c = nextchar()) == '$')
				;
		} while	(charclass[c]==LETTER || charclass[c]==DIGIT);
		if (p - tempbuf > MAXSYMBOLSIZE)
		{
			if (pass2) warnprt (0, 1);
			p = tempbuf + MAXSYMBOLSIZE;
		}
		*p++ = '\0';
		peekc = c;
		return(tokenofitem(UNDECLARED));
	case HEXIN:
	{
		int corig = c;
		if (*ifptr) return (skipline(c));
		while ((c = nextchar ()) == '$');
		if (!numpart[c])
		{
			peekc = c;
			return (corig);
		}
		leadinghex = 1;
		/* fall through */
	}
	case DIGIT:
		if (*ifptr) return (skipline(c));
		p = tempbuf;
		do	{
			if (p >= tempmax)
				error(symlong);
			*p++ = (c >= 'A' && c <= 'Z') ? c + 'a' - 'A' : c;
			while	((c = nextchar()) == '$');
			}
			while(numpart[c]);
		peekc = c;
		if (leadinghex)
		{
			*p++ = 'h';
		}
		*p-- = '\0';
		switch(*p)	{
			case 'o':
			case 'q':
				radix = 8;
				limit = 020000;
				*p = '\0';
				break;
			case 'd':
				radix = 10;
				limit = 6553;
				*p = '\0';
				break;
			case 'h':
				radix = 16;
				limit = 010000;
				*p = '\0';
				break;
			case 'b':
				radix = 2;
				limit = 077777;
				*p = '\0';
				break;
			default:
				radix = 10;
				limit = 6553;
				p++;
				break;
			}

		/*
		 *  tempbuf now points to the number, null terminated
		 *  with radix 'radix'.
		 */
		yylval.ival = 0;
		p = tempbuf;
		do	{
			c = *p - (*p > '9' ? ('a' - 10) : '0');
			if (c >= radix)
				{
				err[iflag]++;
				yylval.ival = 0;
				break;
				}
			if (yylval.ival < limit ||
				(radix == 10 && yylval.ival == 6553 && c < 6) ||
				(radix == 2 && yylval.ival == limit))
				yylval.ival = yylval.ival * radix + c;
			else {
				err[vflag]++;
				yylval.ival = 0;
				break;
				}
			}
			while(*++p != '\0');
		return(NUMBER);
	default:
		if (*ifptr)
			return(skipline(c));
		switch(c) {
		int corig;
		case ';':
			return(skipline(c));
		case '\'':
			if (quoteflag) return('\'');
		case '"':
			corig = c;
			p = tempbuf;
			p[1] = 0;
			do	switch(c = nextchar())	{
			case '\0':
			case '\n':
				err[bflag]++;
				goto retstring;
			case '\'':
			case '"':
				if (c == corig && (c = nextchar()) != corig) {
				retstring:
					peekc = c;
					*p = '\0';
					if ((p-tempbuf) >2) {
						yylval.cval = tempbuf;
						return(STRING);
					} else if (p-tempbuf == 2)	{
						p = tempbuf;
						yylval.ival = *p++ ;
						yylval.ival |= *p<<8;
						return(TWOCHAR);
					} else	{
						p = tempbuf;
						yylval.ival = *p++;
						return(ONECHAR);
					}
				}
			default:
				*p++ = c;
			} while (p < tempmax);
			/*
			 *  if we break out here, our string is longer than
			 *  our input line
			 */
			error("string buffer overflow");
		case '<':
			corig = c;
			switch (c = nextchar ()) {
			case '=':
				return LE;
			case '<':
				return SHL;
			case '>':
				return NE;
			default:
				peekc = c;
				return corig;
			}
			/* break; suppress "unreachable" warning for tcc */
		case '>':
			corig = c;
			switch (c = nextchar ()) {
			case '=':
				return GE;
			case '>':
				return SHR;
			default:
				peekc = c;
				return corig;
			}
			/* break; suppress "unreachable" warning for tcc */
		case '!':
			corig = c;
			switch (c = nextchar ()) {
			case '=':
				return NE;
			default:
				peekc = c;
				return corig;
			}
			/* break; suppress "unreachable" warning for tcc */
		case '=':
			corig = c;
			switch (c = nextchar ()) {
			case '=':
				return EQ;
			default:
				peekc = c;
				return corig;
			}
			/* break; suppress "unreachable" warning for tcc */
		default:
			return(c);
		}
	}
}

/*
 *  return the token associated with the string pointed to by
 *  tempbuf.  if no token is associated with the string, associate
 *  deftoken with the string and return deftoken.
 *  in either case, cause yylval to point to the relevant
 *  symbol table entry.
 */

int tokenofitem(int deftoken)
{
	char *p;
	struct item *	ip;
	int  i;
	int  r, l, u, hash;


#ifdef T_DEBUG
	fputs("'tokenofitem entry'	", stderr) ;
	fputs(tempbuf, stderr) ;
#endif
	if (strcmp (tempbuf, "cmp") == 0 ||
	    strcmp (tempbuf, "jmp") == 0 ||
	    strcmp (tempbuf, "v")   == 0 ||
	    strcmp (tempbuf, "nv")  == 0)
		if (pass2) warnprt (1, 1);
	/*
	 *  binary search
	 */
	l = 0;
	u = (sizeof keytab/sizeof keytab[0])-1;
	while (l <= u) {
		i = (l+u)/2;
		ip = &keytab[i];
		if ((r = strcmp(tempbuf, ip->i_string)) == 0)
			goto found;
		if (r < 0)
			u = i-1;
		else
			l = i+1;
	}

	/*
	 *  hash into item table
	 */
	hash = 0;
	p = tempbuf;
	while (*p) hash += *p++;
	hash %= ITEMTABLESIZE;
	ip = &itemtab[hash];

	loop {
		if (ip->i_token == 0)
			break;
		if (strcmp(tempbuf, ip->i_string) == 0)
			goto found;
		if (++ip >= itemmax)
			ip = itemtab;
	}

	if (!deftoken) {
		i = 0 ;
		goto token_done ;
	}
	if (++nitems > ITEMTABLESIZE-20)
		error("item table overflow");
	ip->i_string = malloc(strlen(tempbuf)+1);
	ip->i_token = deftoken;
	ip->i_uses = 0;
	ip->i_equbad = 0;
	strcpy(ip->i_string, tempbuf);

found:
	if (*ifptr) {
		if (ip->i_token == ENDIF) {
			i = ENDIF ;
			goto token_done ;
		}
		if (ip->i_token == ELSE) {
			/* We must only honour the ELSE if it is not
			   in a nested failed IF/ELSE */
			char forbid = 0;
			char *ifstackptr;
			for (ifstackptr = ifstack; ifstackptr != ifptr; ++ifstackptr) {
				if (*ifstackptr) {
					forbid = 1;
					break;
				}
			}
			if (!forbid) {
				i = ELSE;
				goto token_done;
			}
		}
		if (ip->i_token == IF) {
			if (ifptr >= ifstmax)
				error("Too many ifs");
			else *++ifptr = 1;
		}
		i = skipline(' ');
		goto token_done ;
	}
	yylval.itemptr = ip;
	i = ip->i_token;
	if (i == EQU) equ_bad_label=0;
token_done:
#ifdef T_DEBUG
	fputs("\t'tokenofitem exit'\n", stderr) ;
#endif
	return(i) ;
}


/*
 *  interchange two entries in the item table -- used by custom_qsort
 */
void interchange(int i, int j)
{
	struct item *fp, *tp;
	struct item temp;

	fp = &itemtab[i];
	tp = &itemtab[j];
	temp.i_string = fp->i_string;
	temp.i_value = fp->i_value;
	temp.i_token = fp->i_token;
	temp.i_uses = fp->i_uses;
	temp.i_equbad = fp->i_equbad;

	fp->i_string = tp->i_string;
	fp->i_value = tp->i_value;
	fp->i_token = tp->i_token;
	fp->i_uses = tp->i_uses;
	fp->i_equbad = tp->i_equbad;

	tp->i_string = temp.i_string;
	tp->i_value = temp.i_value;
	tp->i_token = temp.i_token;
	tp->i_uses = temp.i_uses;
	tp->i_equbad = temp.i_equbad;
}



/*
 *  quick sort -- used by putsymtab to sort the symbol table
 */
void custom_qsort(int m, int n)
{
	int  i, j;

	if (m < n) {
		i = m;
		j = n+1;
		loop {
			do i++; while(strcmp(itemtab[i].i_string,
					itemtab[m].i_string) < 0);
			do j--; while(strcmp(itemtab[j].i_string,
					itemtab[m].i_string) > 0);
			if (i < j) interchange(i, j); else break;
		}
		interchange(m, j);
		custom_qsort(m, j-1);
		custom_qsort(j+1, n);
	}
}



/*
 *  get the next character
 */
int nextchar()
{
	int c, ch;
	static  char  *earg;

	if (peekc != -1) {
		c = peekc;
		peekc = -1;
		return(c);
	}

start:
	if (earg) {
		if (*earg)
			return(addtoline(*earg++));
		earg = 0;
	}

	if (expptr) {
		if ((ch = getm()) == '\1') {	/*  expand argument  */
			ch = getm() - 'A';
			if (ch >= 0 && ch < PARMMAX && est[ch])
				earg = est[ch];
			goto start;
		}
		if (ch == '\2') {	/*  local symbol  */
			ch = getm() - 'A';
			if (ch >= 0 && ch < PARMMAX && est[ch]) {
				earg = est[ch];
				goto start;
			}
			earg = getlocal(ch, (int)(long)est[TEMPNUM]);
			goto start;
		}

		return(addtoline(ch));
	}
	ch = getc(now_file) ;
	/* if EOF, check for include file */
	if (ch == EOF) {
		while (ch == EOF && now_in) {
			fclose(fin[now_in]) ;
			free(src_name[now_in]) ;
			now_file = fin[--now_in] ;
			ch = getc(now_file) ;
		}
		if (linein[now_in] < 0) {
			lstoff = 1 ;
			linein[now_in] = -linein[now_in] ;
		} else {
			lstoff = 0 ;
		}
		if (pass2 && iflist()) {
			lineout() ;
			fprintf(fout, "**** %s ****\n", src_name[now_in]) ;
		}
	}
	if (ch == '\n')
	{
		linein[now_in]++ ;
	}

	return(addtoline(ch)) ;
}


/*
 *  skip to rest of the line -- comments and if skipped lines
 */
int skipline(int ac)
{
	int  c;

	c = ac;
	while (c != '\n' && c != '\0')
		c = nextchar();
	return('\n');
}



void usage()
{
	printf(
"zmac " ZMAC_VERSION
#ifdef ZMAC_BETA
	ZMAC_BETA
#endif
", a Z80 macro cross-assembler.\n"
"Public domain by Bruce Norskog and others.\n"
"\n"
#ifdef __riscos
"usage: zmac [--help] [--version] [-AbcdefghilLmnOpsStTz]\n"
#else
"usage: zmac [--help] [--version] [-AbcdefghilLmnOpsStz]\n"
#endif
"       [-o outfile] [-x listfile] [filename]\n"
"\n"
"       --help  give this usage help.\n"
"       --version  report version number.\n"
"       -A      output AMSDOS binary file rather than default binary file.\n"
"       -b      don't generate the m/c output at all.\n"
"       -c      make the listing continuous, i.e. don't generate any\n"
"               page breaks or page headers. (This is the default.)\n"
"       -d      make the listing discontinuous.\n"
"       -e      omit the `error report' section in the listing.\n"
"       -f      list instructions not assembled due to `if' expressions being\n"
"               false. (Normally these are not shown in the listing.)\n"
"       -g      list only the first line of equivalent hex for a source line.\n"
"       -h      output CP/M-ish Intel hex format (using extension `.hex')\n"
"               rather than default binary file (extension `.bin').\n"
"       -i      don't list files included with `include'.\n"
"       -l      don't generate a listing at all.\n"
"       -L      generate listing; overrides any conflicting options.\n"
"       -m      list macro expansions.\n"
"       -n      omit line numbers from listing.\n"
"       -o      assemble output to `outfile'.\n"
"       -O      suggest possible optimisations (as warnings).\n"
"       -p      use linefeeds for page break in listing rather than ^L.\n"
"       -s      omit the symbol table from the listing.\n"
"       -S      show relevant line when reporting errors.\n"
"       -t      give terse (single-letter) error codes in listing.\n"
#ifdef __riscos
"       -T      enable DDE throwback for reporting warnings and errors.\n"
#endif
"       -x      generate listing to `listfile' (`-' for stdout).\n"
"       -z      accept 8080-compatible instructions only; flag any\n"
"               Z80-specific ones as errors.\n");
}



int main(int argc, char *argv[])
{
	struct item *ip;
	int  i, c;

#ifdef ZMAC_BETA
	printf ("*** THIS IS A BETA VERSION; NOT FOR GENERAL DISTRIBUTION ***\n");
#endif

	if(argc==1)
		usage(),exit(0);

	if(argc>=2) {
		if(strcmp(argv[1],"--help")==0)
			usage(),exit(0);
		else if(strcmp(argv[1],"--version")==0)
			puts("zmac " ZMAC_VERSION
#ifdef ZMAC_BETA
					ZMAC_BETA
#endif
				),exit(0);
	}

	fout = stdout ;
	fin[0] = stdin ;
	now_file = stdin ;

	*bin = *listf = 0;
	optnerr = 0;

	while((c = getoptn(argc,argv,
#ifdef __riscos
		"AbcdefghilLmno:OpsStTx:z"
#else
		"AbcdefghilLmno:OpsStx:z"
#endif
		)) != EOF) {
		switch(c) {

		case 'A':	/*  AMSDOS binary -mep */
			output_amsdos = 1;
			output_hex = 0;
			break;

		case 'b':	/*  no binary  */
			bopt = 0;
			break;

		case 'c':	/*  continuous listing  */
			continuous_listing = 1;
			break;

		case 'd':	/*  discontinuous listing  */
			continuous_listing = 0;
			break;

		case 'e':	/*  error list only  */
			eopt = 0;
			edef = 0;
			break;

		case 'f':	/*  print if skipped lines  */
			fopt++;
			fdef++;
			break;

		case 'g':	/*  do not list extra code  */
			gopt = 0;
			gdef = 0;
			break;

		case 'h':	/* output .hex not .bin -rjm */
			output_hex = 1;
			output_amsdos = 0;
			break;

		case 'i':	/* do not list include files */
			iopt = 1 ;
			break;

		case 'l':	/*  no list  */
			lopt++;
			break;

		case 'L':	/*  force listing of everything */
			lston++;
			break;

		case 'm':	/*  print macro expansions  */
			mdef++;
			mopt++;
			break;

		case 'n':	/*  put line numbers off */
			nopt-- ;
			break;

		case 'o':	/*  specify m/c output file */
			strcpy(bin, optnarg);
			break;

		case 'O':	/*  suggest optimisations  */
			suggest_optimise = 1;
			break;

		case 'p':	/*  put out four \n's for eject */
			popt-- ;
			break;

		case 's':	/*  don't produce a symbol list  */
			sopt++;
			break;

		case 'S':	/*  show line which caused error */
			show_error_line = 1;
			break;

		case 't':	/*  terse error messages in listing  */
			terse_lst_errors = 1;
			break;

#ifdef __riscos
		case 'T':	/*  RISC OS throwback  -mep */
			riscos_thbk = 1;
			break;
#endif

		case 'x':	/*  specify listing file */
			if(strcmp(optnarg, "-") == 0)
				oldoopt++;	/* list to stdout (old `-o') */
			else
				strcpy(listf, optnarg);
			break;

		case 'z':	/*  8080-compatible ops only  */
			output_8080_only = 1;
			break;

		case '?':
		default:	/*  error  */
			justerror("Unknown option or missing argument");
			break;

		}
	}

	if(optnind != argc-1) justerror("Missing, extra or mispositioned argument");

	atexit (doatexit);

	sourcef = argv[optnind];
	strcpy(src, sourcef);

	if ((now_file = fopen(src, "r")) == NULL)
	{
		/* If filename has no pre-existing suffix, then try .z */
		suffix_if_none (src, "z");
		if ((now_file = fopen(src, "r")) == NULL)
			fileerror("Cannot open source file", src);
	}
	now_in = 0 ;
	fin[now_in] = now_file ;
	src_name[now_in] = src ;
#ifdef __riscos
	riscos_set_csd(src); /* -mep */
#endif

	/* If we haven't got a bin file filename, then create one from the
	 * source filename (.hex extension if option -h is specified).
	 */
	if (*bin == 0) {
		strcpy(bin, sourcef);
		if (output_hex)
			suffix(bin,"hex");
		else
			suffix(bin,"bin");
	}
	if (bopt)
		if (( fbuf = fopen(bin, output_hex ? "w" : "wb")) == NULL)
			fileerror("Cannot create binary file", bin);
	if (output_amsdos)
		for(i=0; i<128; i++)
			putc(0,fbuf); /* -mep */

	if (!lopt && !oldoopt) {
		/* If we've not got a filename for the listing file
		 * (-x option) then create one from the source filename
		 * (.lst extension)
		 */
		if( *listf == 0 ) {
			strcpy(listf, sourcef);
			suffix(listf,"lst");
		}
		if ((fout = fopen(listf, "w")) == NULL)
			fileerror("Cannot create list file", listf);
	} else
		fout = stdout ;

	strcpy(mtmp, sourcef);
	suffix(mtmp,"tmp");
	mfile = mfopen(mtmp,"w+b") ;
	if (mfile == NULL) {
		fileerror("Cannot create temp file", mtmp);
	}

	/*
	 *  get the time
	 */
	time(&now);
	timp = ctime(&now);
	timp[16] = 0;
	timp[24] = 0;

	title = sourcef;
	/*
	 * pass 1
	 */
#ifdef DEBUG
	fputs("DEBUG-pass 1\n", stderr) ;
#endif
	setvars();
	yyparse();
	pass2++;
	ip = &itemtab[-1];
	while (++ip < itemmax) {
		/* reset use count */
		ip->i_uses = 0 ;

		/* set macro names, equated and defined names */
		switch	(ip->i_token) {
		case MNAME:
			ip->i_token = OLDMNAME;
			break;

		case EQUATED:
			ip->i_token = WASEQUATED;
			break;

		case DEFLED:
			ip->i_token = UNDECLARED;
			break;
		}
	}
	setvars();
	fseek(now_file, (long)0, 0);

#ifdef DEBUG
	fputs("DEBUG- pass 2\n", stderr) ;
#endif
	yyparse();


	if (bopt) {
		flushbin();
		if (output_hex) {
			putc(':', fbuf);
			if (xeq_flag) {
				puthex(0, fbuf);
				puthex(xeq >> 8, fbuf);
				puthex(xeq, fbuf);
				puthex(1, fbuf);
				puthex(255-(xeq >> 8)-xeq, fbuf);
			} else
				for	(i = 0; i < 10; i++)
					putc('0', fbuf);
			putc('\n', fbuf);
		}
		if (output_amsdos) {
			char leafname[] = "FILENAMEBIN";
			unsigned int chk;
			unsigned int filelen = dollarsign - first_org_store;
			if (filelen & 0x7f)
			{
				putc (0x1a, fbuf); /* CP/M EOF char */
			}
			rewind(fbuf);
			chk=0;
			putc(0,fbuf);
			for(i=0;i<11;i++) {
				putc(leafname[i],fbuf);
				chk+=leafname[i];
			}
			for(i=0;i<6;i++)
				putc(0,fbuf);
			putc(2,fbuf); /* Unprotected binary */
			chk+=2;
			putc(0,fbuf);
			putc(0,fbuf);
			putc(first_org_store & 0xFF,fbuf);
			chk+=first_org_store & 0xFF;
			putc(first_org_store >> 8,fbuf);
			chk+=first_org_store >> 8;
			putc(0,fbuf);
			putc(filelen & 0xFF,fbuf);
			chk+=filelen & 0xFF;
			putc(filelen >> 8,fbuf);
			chk+=filelen >> 8;
			/* Next bit should be entry address really */
			putc(first_org_store & 0xFF,fbuf);
			chk+=first_org_store & 0xFF;
			putc(first_org_store >> 8,fbuf);
			chk+=first_org_store >> 8;
			for(i=28;i<64;i++)
				putc(0,fbuf);
			putc(filelen & 0xFF,fbuf);
			chk+=filelen & 0xFF;
			putc(filelen >> 8,fbuf);
			chk+=filelen >> 8;
			putc(0,fbuf); /* this would be used if length>64K */
			putc(chk & 0xFF,fbuf);
			putc(chk >> 8,fbuf);
		}
		fflush(fbuf);
	}

	if (!lopt)
		fflush(fout);
	if (*writesyms)
		outsymtab(writesyms);
	if (eopt)
		erreport();
	if (!lopt && !sopt)
		putsymtab();
	if (!lopt) {
		eject();
		fflush(fout);
	}
	exit(had_errors);
	return(had_errors); /* main () does return int, after all... */
}


void doatexit (void)
{
#ifdef __riscos
	if (riscos_throwback_started)
	{
		_swix(DDEUtils_ThrowbackEnd,0);
	}
	_swix(DDEUtils_Prefix,1,0); /* Unset CSD */
#endif
}


/*
 *  set some data values before each pass
 */
void setvars()
{
	int  i;

	peekc = -1;
	linein[now_in] = linecnt = 0;
	exp_number = 0;
	emitptr = emitbuf;
	lineptr = linebuf;
	ifptr = ifstack;
	expifp = expif;
	*ifptr = 0;
	dollarsign = 0;
	olddollar = 0;
	phaseflag = 0;
	for (i=0; i<FLAGS; i++) err[i] = 0;
}



/*
 *  print out an error message and die
 */
void error(char *as)
{
	*linemax = 0;
	fprintf(fout, "%s\n", linebuf);
	fflush(fout);
	fprintf(stderr, "%s\n", as) ;
	exit(1);
}


/*
 *  alternate version
 */
void fileerror(char *as,char *filename)
{
	*linemax = 0;
	if (fout != NULL && fout != stdout)
		fprintf(fout, "%s\n", linebuf);
	fflush(fout);
	fprintf(stderr, "%s `%s'\n", as, filename) ;
	exit(1);
}



/*
 *  alternate alternate version
 */
void justerror(char *as)
{
	fprintf(stderr, "%s\n", as) ;
	exit(1);
}


/*
 *  output the symbol table
 */
void putsymtab()
{
	struct item *tp, *fp;
	int  i, j, k, t, rows;
	char c, c1 ;

	if (!nitems)
		return;

	/* compact the table so unused and UNDECLARED entries are removed */
	tp = &itemtab[-1];
	for (fp = itemtab; fp<itemmax; fp++) {
		if (fp->i_token == UNDECLARED) {
			nitems--;
			continue;
		}
		if (fp->i_token == 0)
			continue;
		tp++;
		if (tp != fp) {
			tp->i_string = fp->i_string;
			tp->i_value = fp->i_value;
			tp->i_token = fp->i_token;
			tp->i_uses = fp->i_uses ;
			tp->i_equbad = fp->i_equbad ;
		}
	}

	tp++;
	tp->i_string = "{";

	/*  sort the table */
	custom_qsort(0, nitems-1);

	title = "**  Symbol Table  **";

	rows = (nitems+3) / 3;
	if (rows+5+line > 60)
		eject();
	lineout();
	fprintf(fout,"\n\n\nSymbol Table:\n\n");
	line += 4;

	for (i=0; i<rows; i++) {
		for(j=0; j<3; j++) {
			k = rows*j+i;
			if (k < nitems) {
				tp = &itemtab[k];
				t = tp->i_token;
				c = ' ' ;
				if (t == EQUATED || t == DEFLED)
					c = '=' ;
				if (tp->i_uses == 0)
					c1 = '+' ;
				else
					c1 = ' ' ;
				fprintf(fout, "%-15s%c%4x%c    ",
					tp->i_string, c, tp->i_value & 0xffff, c1);
			}
		}
		lineout();
		putc('\n', fout);
	}
}




/*
 *  put out error report
 */
void erreport()
{
	int i, numerr;

	if (line > 50) eject();
	lineout();
	numerr = 0;
	for (i=0; i<FLAGS; i++) numerr += keeperr[i];
	if (numerr) {
		fputs("\n\n\nError report:\n\n", fout);
		fprintf(fout, "%6d error%s\n", DO_PLURAL(numerr));
		line += 5;
	} else {
		fputs("\n\n\nStatistics:\n", fout);
		line += 3;
	}

	for (i=0; i<FLAGS; i++)
		if (keeperr[i]) {
			lineout();
			if (terse_lst_errors)
				/* no plural on this because it would
				 * odd, I think. -rjm
				 */
				fprintf(fout, "%6d %c -- %s error\n",
					keeperr[i], errlet[i], errname[i]);
			else
				/* can't use DO_PLURAL for this due to
				 * the %s in the middle... -rjm
				 */
				fprintf(fout, "%6d %s error%s\n",
					keeperr[i], errname[i],
					(keeperr[i]==1)?"":"s");
		}

	if (line > 55) eject();
	lineout();
	fprintf(fout, "\n%6d\tsymbol%s\n", DO_PLURAL(nitems));
	fprintf(fout, "%6d\tbyte%s\n", DO_PLURAL(nbytes));
	line += 2;
	if (mfptr) {
		if (line > 53) eject();
		lineout();
		fprintf(fout, "\n%6d\tmacro call%s\n", DO_PLURAL(exp_number));
		fprintf(fout, "%6d\tmacro byte%s\n", DO_PLURAL(mfptr));
		fprintf(fout, "%6d\tinvented symbol%s\n",
						DO_PLURAL(invented/2));
		line += 3;
	}
}


/*
 *  lexical analyser for macro definition
 */
void mlex()
{
	char  *p;
	int  c;
	int  t;

	/*
	 *  move text onto macro file, changing formal parameters
	 */
#ifdef	M_DEBUG
	fprintf(stderr,"enter 'mlex'\n") ;
#endif
	inmlex++;

	c = nextchar();
loop {
	switch(charclass[c]) {

	case DIGIT:
		while (numpart[c]) {
			putm(c);
			c = nextchar();
		}
		continue;

	case STARTER:
	case LETTER:
		t = 0;
		p = tempbuf+MAXSYMBOLSIZE+2;
		do {
			if (p >= tempmax)
				error(symlong);
			*p++ = c;
			if (t < MAXSYMBOLSIZE)
				tempbuf[t++] = (c >= 'A' && c <= 'Z')  ?
					c+'a'-'A' : c;
			else
				if (pass2) warnprt (0, 1);
			c = nextchar();
		} while (charclass[c]==LETTER || charclass[c]==DIGIT);

		tempbuf[t] = 0;
		*p++ = '\0';
		p = tempbuf+MAXSYMBOLSIZE+2;
		t = tokenofitem(0);
		if (t != MPARM) while (*p) putm(*p++);
		else {
			if (*(yylval.itemptr->i_string) == '?') putm('\2');
			else putm('\1');
			putm(yylval.itemptr->i_value + 'A');
		}
		if (t == ENDM) goto done;
		continue;

	case F_END:
		if (expptr) {
			popsi();
			c = nextchar();
			continue;
		}

		goto done;

	default:
		if (c == '\n') {
			linecnt++;
		}
		if (c != '\1') putm(c);
		c = nextchar();
	}
}

	/*
	 *  finish off the file entry
	 */
done:
	while(c != EOF && c != '\n' && c != '\0') c = nextchar();
	linecnt++;
	putm('\n');
	putm('\n');
	putm(0);

	for (c=0; c<ITEMTABLESIZE; c++)
		if (itemtab[c].i_token == MPARM) {
			itemtab[c].i_token = UNDECLARED;
		}
	inmlex = 0;
#ifdef	M_DEBUG
	fprintf(stderr,"exit 'mlex'\n") ;
#endif
}



/*
 *  lexical analyser for the arguments of a macro call
 */
int getarg()
{
	int c;
	char *p;
	static int comma;

	*tempbuf = 0;
	yylval.cval = tempbuf;
	while(charclass[c = nextchar()] == SPACE);

	switch(c) {

	case '\0':
		popsi();
	case '\n':
	case ';':
		comma = 0;
		return(skipline(c));

	case ',':
		if (comma) {
			comma = 0;
			return(',');
		}
		else {
			comma++;
			return(ARG);
		}

	case '\'':
		p = tempbuf;
		do switch (c = nextchar()) {
			case '\0':
			case '\n':
				peekc = c;
				*p = 0;
				err[bflag]++;
				return(ARG);
			case '\'':
				if ((c = nextchar()) != '\'') {
					peekc = c;
					*p = '\0';
					comma++;
					return(ARG);
				}
			default:
				*p++ = c;
		} while (p < tempmax);
		error(symlong);		/* doesn't return */

	default:  /* unquoted string */
		p = tempbuf;
		peekc = c;
		do switch(c = nextchar()) {
			case '\0':
			case '\n':
			case '\t':
			case ' ':
			case ',':
				peekc = c;
				*p = '\0';
				comma++;
				return(ARG);
			default:
				*p++ = c;
		} while (p < tempmax);
	}

	/* in practice it can't get here, but FWIW and to satisfy
	 * -Wall... -rjm */
	error("can't happen - in zmac.y:getarg(), infinite unquoted string!?");
	return(0);
}



/*
 * Add suffix to pathname if leafname doesn't already have a suffix.
 * The suffix passed should not include an extension separator.
 * The pathname passed should be in local format.
 */
void suffix_if_none (char *str, char *suff)
{
	char *leafname, *extension;

	leafname = strrchr (str, OS_DIR_SEP);
	if (leafname == NULL)
	{
		leafname = str;
	}

	extension = strchr (leafname, OS_EXT_SEP);
	if (extension == NULL)
	{
		size_t leafsize = strlen (leafname);

		leafname[leafsize] = OS_EXT_SEP;
		strcpy (leafname + leafsize + 1, suff);
	}
}



/*
 * Add or change pathname suffix.
 * The suffix passed should not include an extension separator.
 * The pathname passed should be in local format.
 * If the leafname passed has more than one extension, the last is changed.
 */
void suffix (char *str, char *suff)
{
	char *leafname, *extension;

	leafname = strrchr (str, OS_DIR_SEP);
	if (leafname == NULL)
	{
		leafname = str;
	}

	extension = strrchr (leafname, OS_EXT_SEP);
	if (extension == NULL)
	{
		extension = leafname + strlen (leafname);
	}

	*extension = OS_EXT_SEP;
	strcpy (extension + 1, suff);
}



/*
 * Decanonicalise a canonical pathname.
 * A canonical pathname uses '/' as the directory separator,
 * '.' as the extension separator, ".." as the parent directory,
 * "." as the current directory, and a leading '/' as the root
 * directory (it would be more user-friendly not to use this!).
 */
void decanonicalise (char *pathname)
{
#if defined (MSDOS)

	char *directory = pathname;

	/* Just need to change all '/'s to '\'s */

	while ((directory = strchr (directory, '/')) != NULL)
	{
		*directory = OS_DIR_SEP;
	}

#elif defined (__riscos)

	char *directory = pathname, *dirend;

	/* First deal with leading '/' */

	if (*directory == '/')
	{
		memmove (directory + 1, directory, strlen (directory) + 1);
		*directory = '$';
		++directory;
	}

	/* Then deal with non-leaf ".."s and "."s */

	while (1)
	{
		dirend = strchr (directory, '/');
		if (dirend == NULL)
		{
			break;
		}

		*dirend = '\0';

		if (strcmp (directory, "..") == 0)
		{
			*directory = '^';
			memmove (directory + 2, directory + 3, strlen (directory + 3) + 1);
			dirend = directory + 1;
		}
		else if (strcmp (directory, ".") == 0)
		{
			memmove (directory, directory + 2, strlen (directory + 2) + 1);
			continue;
		}

		*dirend = '/';
		directory = dirend + 1;
	}

	directory = pathname;

	/* Finally, swap '/' and '.' */

	while ((directory = strpbrk (directory, "/.")) != NULL)
	{
		if (*directory == '/')
		{
			*directory = OS_DIR_SEP;
		}
		else
		{
			*directory = OS_EXT_SEP;
		}
		++directory;
	}

#else

	/* Local form is canonical form */

	UNUSED (pathname);

#endif
}



/*
 *  put out a byte to the macro file, keeping the offset
 */
void putm(char c)
{
#ifdef M_DEBUG
	fputc (c, stderr);
#endif
	mfptr++;
	mfputc(c,mfile) ;
}



/*
 *  get a byte from the macro file
 */
int getm()
{
	int ch;

	floc++;
	ch = mfgetc(mfile) ;
	if (ch == EOF) {
		ch = 0;
		fprintf(stderr,"bad macro read\n") ;
	}
	return(ch);
}



/*
 *  pop standard input
 */
void popsi()
{
	int  i;

	if (est)
	{
		for (i=0; i<PARMMAX; i++) {
			if (est[i])
#ifdef M_DEBUG
	fprintf (stderr, "[Freeing arg%u(%p)]\n", i, est[i]),
#endif
					free(est[i]);
		}
		floc = est[FLOC];
		free(est);
		expptr--;
		est = expptr ? (char **) expstack[expptr-1] : (char **) 0;
		mfseek(mfile, (long)floc, 0);
		if (lineptr > linebuf) lineptr--;
	}
}



/*
 *  return a unique name for a local symbol
 *  c is the parameter number, n is the macro number.
 */

char *getlocal(int c, int n)
{
	static char local_label[10];

	invented++;
	if (c >= 26)
		c += 'a' - '0';
	sprintf(local_label, "?%c%04d", c+'a', n) ;
	return(local_label);
}



/*
 *  read in a symbol table
 */
void insymtab(char *name)
{
	struct stab *t;
	int  s, i;
	FILE *sfile;

	t = (struct stab *) tempbuf;
	decanonicalise (name);
	if ((sfile = fopen(name, "rb")) == NULL)
		return;
	fread((char *)t, 1, sizeof *t, sfile);
	if (t->t_value != SYMMAJIC)
	{
		fclose (sfile);
		return;
	}

	s = t->t_token;
	for (i=0; i<s; i++) {
		fread((char *)t, 1, sizeof *t, sfile);
		if (tokenofitem(UNDECLARED) != UNDECLARED)
			continue;
		yylval.itemptr->i_token = t->t_token;
		yylval.itemptr->i_value = t->t_value;
		if (t->t_token == MACRO)
			yylval.itemptr->i_value += mfptr;
	}

	while ((s = fread(tempbuf, 1, TEMPBUFSIZE, sfile)) > 0) {
		mfptr += s;
		mfwrite(tempbuf, 1, s, mfile) ;
	}

	fclose (sfile);
}



/*
 *  write out symbol table
 */
void outsymtab(char *name)
{
	struct stab *t;
	struct item *ip;
	int  i;
	FILE *sfile;

	t = (struct stab *) tempbuf;
	decanonicalise (name);
	if ((sfile = fopen(name, "wb")) == NULL)
		return;
	for (ip=itemtab; ip<itemmax; ip++) {
		if (ip->i_token == UNDECLARED) {
			ip->i_token = 0;
			nitems--;
		}
	}

	copyname(title, (char *)t);
	t->t_value = SYMMAJIC;
	t->t_token = nitems;
	fwrite((char *)t, 1, sizeof *t, sfile);

	for (ip=itemtab; ip<itemmax; ip++) {
		if (ip->i_token != 0) {
			t->t_token = ip->i_token;
			t->t_value = ip->i_value;
			copyname(ip->i_string, (char *)t);
			fwrite((char *)t, 1, sizeof *t, sfile);
		}
	}

	mfseek(mfile, (long)0, 0);
	while((i = mfread(tempbuf, 1, TEMPBUFSIZE, mfile) ) > 0)
		fwrite(tempbuf, 1, i, sfile);

	fclose (sfile);
}



/*
 *  copy a name into the symbol file
 */
void copyname(char *st1, char *st2)
{
	char  *s1, *s2;
	int  i;

	i = (MAXSYMBOLSIZE+2) & ~01;
	s1 = st1;
	s2 = st2;

	while((*s2++ = *s1++)) i--;		/* -Wall-ishness :-) -RJM */
	while(--i > 0) *s2++ = '\0';
}

/* get the next source file */
void next_source(char *sp)
{

	if(now_in == NEST_IN -1)
		error("Too many nested includes") ;
	decanonicalise (sp);
	if ((now_file = fopen(sp, "r")) == NULL) {
#ifdef __riscos
		if (riscos_thbk)
			riscos_throwback(2,src_name[now_in],linein[now_in],"Cannot open include file");
#endif
		fileerror("Cannot open include file", sp) ;
	}
	if (pass2 && iflist()) {
		lineout() ;
		fprintf(fout, "**** %s ****\n",sp) ;
	}

	/* save the list control flag with the current line number */
	if (lstoff)
		linein[now_in] = - linein[now_in] ;

	/* no list if include files are turned off */
	lstoff |= iopt ;

	/* save the new file descriptor. */
	fin[++now_in] = now_file ;
	/* start with line 0 */
	linein[now_in] = 0 ;
	/* save away the file name */
	src_name[now_in] = malloc(strlen(sp)+1) ;
	strcpy(src_name[now_in],sp) ;
}

#ifdef __riscos
/*
 * On entry sp should point to the full pathname of a file in RISC OS form.
 * Searches for the last dot, and sets the local CSD to that path.
 * Does not corrupt the string.
 */
void riscos_set_csd(char *sp)
{
	char *s1 = strrchr (sp, '.');

	if (s1 != NULL)
	{
		*s1=0;
		_swix(DDEUtils_Prefix,1,sp);
		*s1='.';
	}
}

void riscos_throwback(int severity, char *file, int line, char *error)
{
	if (riscos_throwback_started==0)
	{
		riscos_throwback_started=1;
		*riscos_thbkf=0;
		_swix(DDEUtils_ThrowbackStart,0);
	}
	if (strcmp(file, riscos_thbkf)!=0)
	{
		_swix(DDEUtils_ThrowbackSend,4+1,0,file); /* Notify of a change of file */
		strcpy(riscos_thbkf,file);
	}
	_swix(DDEUtils_ThrowbackSend,32+16+8+4+1,1,file,line,severity,error);
}
#endif
