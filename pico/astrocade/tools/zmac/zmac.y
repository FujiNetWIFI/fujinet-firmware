%{
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

%}

%union	{
	struct item *itemptr;
	int ival;
	char *cval;
	}

%token <cval> STRING
%token <itemptr> NOOPERAND
%token <itemptr> ARITHC
%token ADD
%token <itemptr> LOGICAL
%token <itemptr> AND
%token <itemptr> OR
%token <itemptr> XOR
%token <itemptr> BIT
%token CALL
%token <itemptr> INCDEC
%token <itemptr> DJNZ
%token EX
%token <itemptr> IM
%token PHASE
%token DEPHASE
%token <itemptr> IN
%token JP
%token <itemptr> JR
%token LD
%token <itemptr> OUT
%token <itemptr> PUSHPOP
%token <itemptr> RET
%token <itemptr> SHIFT
%token <itemptr> RST
%token <itemptr> REGNAME
%token <itemptr> ACC
%token <itemptr> C
%token <itemptr> RP
%token <itemptr> HL
%token <itemptr> INDEX
%token <itemptr> AF
%token <itemptr> SP
%token <itemptr> MISCREG
%token F
%token <itemptr> COND
%token <itemptr> SPCOND
%token <ival> NUMBER
%token <itemptr> UNDECLARED
%token END
%token ORG
%token DEFB
%token DEFS
%token DEFW
%token EQU
%token DEFL
%token <itemptr> LABEL
%token <itemptr> EQUATED
%token <itemptr> WASEQUATED
%token <itemptr> DEFLED
%token <itemptr> MULTDEF
%token <ival> MOD
%token <ival> SHL
%token <ival> SHR
%token <ival> NOT
%token <ival> LT
%token <ival> GT
%token <ival> EQ
%token <ival> LE
%token <ival> GE
%token <ival> NE
%token IF
%token ELSE
%token ENDIF
%token <itemptr> ARGPSEUDO
%token <itemptr> LIST
%token <itemptr> MINMAX
%token MACRO
%token <itemptr> MNAME
%token <itemptr> OLDMNAME
%token ARG
%token ENDM
%token MPARM
%token <ival> ONECHAR
%token <ival> TWOCHAR

%type <itemptr> label.part symbol
%type <ival> reg evenreg realreg mem pushable bcdesp bcdehlsp mar condition
%type <ival> spcondition noparenexpr parenexpr expression lxexpression

%left '|' OR
%left '^' XOR
%left '&' AND
%left '=' EQ NE
%left '<' '>' LT GT LE GE
%left SHL SHR
%left '+' '-'
%left '*' '/' '%' MOD
%right '!' '~' NOT UNARY

%%


statements:
	/* Empty file! */
|
	statements statement
;


statement:
	label.part '\n' {
		if ($1) list(dollarsign);
		else  list1();
	}
|
	label.part operation '\n' {
		list(dollarsign);
	}
|
	symbol EQU expression '\n' {
		/* a forward reference to a label in the expression cannot
		 * be fixed in a forward reference to the EQU;
		 * it would need three passes. -rjm
		 */
		if(!pass2 && equ_bad_label) {
			/* this indicates that the equ has an incorrect
			 * (i.e. pass 1) value.
			 */
			$1->i_equbad = 1;
		} else {
			/* but if 2nd pass or no forward reference, it's ok. */
			$1->i_equbad = 0;
		}
		equ_bad_label=0;
		switch($1->i_token) {
		case UNDECLARED: case WASEQUATED:
			$1->i_token = EQUATED;
			$1->i_value = $3;
			break;
		case EQUATED:
			if ($1->i_value == $3)
				break; /* Allow benign redefinition -mgr */
			/* Drop-through intentional */
		default:
			err[mflag]++;
			$1->i_token = MULTDEF;
		}
		list($3);
	}
|
	symbol DEFL expression '\n' {
		switch($1->i_token) {
		case UNDECLARED: case DEFLED:
			$1->i_token = DEFLED;
			$1->i_value = $3;
			break;
		default:
			err[mflag]++;
			$1->i_token = MULTDEF;
		}
		list($3);
	}
|
	symbol MINMAX expression ',' expression '\n' {
		switch ($1->i_token) {
		case UNDECLARED: case DEFLED:
			$1->i_token = DEFLED;
			if ($2->i_value)	/* max */
				list($1->i_value = ($3 > $5? $3:$5));
			else list($1->i_value = ($3 < $5? $3:$5));
			break;
		default:
			err[mflag]++;
			$1->i_token = MULTDEF;
			list($1->i_value);
		}
	}
|
	IF expression '\n' {
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
				if (*ifptr != !($2)) err[pflag]++;
			} else {
				if (expifp >= expifmax)
					error("Too many ifs!");
				*expifp++ = !($2);
				*++ifptr = !($2);
			}
		}
		saveopt = fopt;
		fopt = 1;
		list($2);
		fopt = saveopt;
	}
|
	ELSE '\n' {
		/* FIXME: it would be nice to spot repeated ELSEs, but how? */
		*ifptr = !*ifptr;
		saveopt = fopt;
		fopt = 1;
		list1();
		fopt = saveopt;
	}
|
	ENDIF '\n' {
		if (ifptr == ifstack) err[bflag]++;
		else --ifptr;
		list1();
	}
|
	label.part END '\n' {
		list(dollarsign);
		peekc = 0;
	}
|
	label.part END expression '\n' {
		xeq_flag++;
		xeq = $3;
		list($3);
		peekc = 0;
	}
|
	label.part DEFS expression '\n' {
		if ($3 < 0) err[vflag]++;
		list(dollarsign);
		if ($3) {
			flushbin();
			dollarsign += $3;
			olddollar = dollarsign;

			/* if it's not hex output though, we also need
			 * to output zeroes as appropriate. -rjm
			 */
			if(!output_hex && pass2) {
				int f;
				for (f=0;f<($3);f++)
					fputc(0, fbuf);
			}
		}
	}
|
	ARGPSEUDO arg_on ARG arg_off '\n' {
		list1();
		switch ($1->i_value) {

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
|
	ARGPSEUDO arg_on arg_off '\n' {
		fprintf(stderr,"ARGPSEUDO error\n");
		err[fflag]++;
		list(dollarsign);
	}
|
	LIST '\n' {
		list_tmp1=$1->i_value;
		list_tmp2=1;
		goto dolopt;
	}
|
	LIST expression '\n' {
		list_tmp1=$1->i_value;
		list_tmp2=$2;
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
|
	UNDECLARED MACRO parm.list '\n' {
		$1->i_token = MNAME;
		$1->i_value = mfptr;
#ifdef M_DEBUG
		fprintf (stderr, "[UNDECLARED MACRO %s]\n", $1->i_string);
#endif
		mfseek(mfile, (long)mfptr, 0);
		list1();
		mlex() ;
		parm_number = 0;
	}
|
	OLDMNAME MACRO {
		$1->i_token = MNAME;
#ifdef M_DEBUG
		fprintf (stderr, "[OLDNAME MACRO %s]\n", $1->i_string);
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
|
	label.part MNAME al arg.list '\n' {
#ifdef M_DEBUG
		fprintf (stderr, "[MNAME %s]\n", $2->i_string);
#endif
		$2->i_uses++ ;
		arg_flag = 0;
		parm_number = 0;
		list(dollarsign);
		expptr++;
		est = est2;
		est2 = NULL;
		est[FLOC] = floc;
		est[TEMPNUM] = (char *)(long)exp_number++;
		floc = (char *)(long)($2->i_value);
		mfseek(mfile, (long)floc, 0);
	}
|
	error {
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
;

colonordot:
	':'
|
	'.'
;

maybecolon:
	/* empty */
|
	':'
;

label.part:
	/* empty */
	{	$$ = NULL;	}
|
	colonordot symbol {
		switch($2->i_token) {
		case UNDECLARED:
			if (pass2)
				err[pflag]++;
			else {
				$2->i_token = LABEL;
				$2->i_value = dollarsign;
			}
			break;
		case LABEL:
			if (!pass2) {
				$2->i_token = MULTDEF;
				err[mflag]++;
			} else if ($2->i_value != dollarsign)
				err[pflag]++;
			break;
		default:
			err[mflag]++;
			$2->i_token = MULTDEF;
		}
	}
|
	symbol maybecolon {
		switch($1->i_token) {
		case UNDECLARED:
			if (pass2)
				err[pflag]++;
			else {
				$1->i_token = LABEL;
				$1->i_value = dollarsign;
			}
			break;
		case LABEL:
			if (!pass2) {
				$1->i_token = MULTDEF;
				err[mflag]++;
			} else if ($1->i_value != dollarsign)
				err[pflag]++;
			break;
		default:
			err[mflag]++;
			$1->i_token = MULTDEF;
		}
	}
;


operation:
	NOOPERAND
		{ emit1($1->i_value, 0, 0, 1); }
|
	JP expression
		{ emitjp(0303, $2); }
|
	CALL expression
		{ emit(3, 0315, $2, $2 >> 8); }
|
	RST expression
		{ int a = $2, doneerr=0;
		/* added support for normal RST form -rjm */
		if (a >= 8) {
			if ((a&7)!=0) doneerr=1,err[vflag]++;
			a >>= 3;
		}
		if ((a > 7 || a < 0) && !doneerr) /* don't give two errs... */
			err[vflag]++;
		emit(1, $1->i_value + (a << 3));
	}
|
	ADD expression
		{ emit1(0306, 0, $2, 3); if (pass2) warnprt (1, 0); }
|
	ADD ACC ',' expression
		{ emit1(0306, 0, $4, 3); }
|
	ARITHC expression
		{ emit1(0306 + ($1->i_value << 3), 0, $2, 3); if (pass2) warnprt (1, 0); }
|
	ARITHC ACC ',' expression
		{ emit1(0306 + ($1->i_value << 3), 0, $4, 3); }
|
	LOGICAL expression
		{ emit1(0306 | ($1->i_value << 3), 0, $2, 3); }
|
	AND expression
		{ emit1(0306 | ($1->i_value << 3), 0, $2, 3); }
|
	OR expression
		{ emit1(0306 | ($1->i_value << 3), 0, $2, 3); }
|
	XOR expression
		{ emit1(0306 | ($1->i_value << 3), 0, $2, 3); }
|
	LOGICAL ACC ',' expression	/* -cdk */
		{ emit1(0306 | ($1->i_value << 3), 0, $4, 3); if (pass2) warnprt (1, 0); }
|
	AND ACC ',' expression	/* -cdk */
		{ emit1(0306 | ($1->i_value << 3), 0, $4, 3); if (pass2) warnprt (1, 0); }
|
	OR ACC ',' expression	/* -cdk */
		{ emit1(0306 | ($1->i_value << 3), 0, $4, 3); if (pass2) warnprt (1, 0); }
|
	XOR ACC ',' expression	/* -cdk */
		{ emit1(0306 | ($1->i_value << 3), 0, $4, 3); if (pass2) warnprt (1, 0); }
|
	ADD reg
		{ emit1(0200 + ($2 & 0377), $2, 0, 0); if (pass2) warnprt (1, 0); }
|
	ADD ACC ',' reg
		{ emit1(0200 + ($4 & 0377), $4, 0, 0); }
|
	ARITHC reg
		{ emit1(0200 + ($1->i_value << 3) + ($2 & 0377), $2, 0, 0); if (pass2) warnprt (1, 0); }
|
	ARITHC ACC ',' reg
		{ emit1(0200 + ($1->i_value << 3) + ($4 & 0377), $4, 0, 0); }
|
	LOGICAL reg
		{ emit1(0200 + ($1->i_value << 3) + ($2 & 0377), $2, 0, 0); }
|
	AND reg
		{ emit1(0200 + ($1->i_value << 3) + ($2 & 0377), $2, 0, 0); }
|
	OR reg
		{ emit1(0200 + ($1->i_value << 3) + ($2 & 0377), $2, 0, 0); }
|
	XOR reg
		{ emit1(0200 + ($1->i_value << 3) + ($2 & 0377), $2, 0, 0); }
|
	LOGICAL ACC ',' reg		/* -cdk */
		{ emit1(0200 + ($1->i_value << 3) + ($4 & 0377), $4, 0, 0); if (pass2) warnprt (1, 0); }
|
	AND ACC ',' reg		/* -cdk */
		{ emit1(0200 + ($1->i_value << 3) + ($4 & 0377), $4, 0, 0); if (pass2) warnprt (1, 0); }
|
	OR ACC ',' reg		/* -cdk */
		{ emit1(0200 + ($1->i_value << 3) + ($4 & 0377), $4, 0, 0); if (pass2) warnprt (1, 0); }
|
	XOR ACC ',' reg		/* -cdk */
		{ emit1(0200 + ($1->i_value << 3) + ($4 & 0377), $4, 0, 0); if (pass2) warnprt (1, 0); }
|
	SHIFT reg
		{
			if (suggest_optimise && pass2 && ($2 & 0377) == 7 && $1->i_value <= 4)
				warnprt ($1->i_value + 4, 0);
			if (pass2 && $1->i_value == 6)
				warnprt (1, 0);
			emit1(0145400 + ($1->i_value << 3) + ($2 & 0377), $2, 0, 0);
		}
|
	INCDEC reg
		{ emit1($1->i_value + (($2 & 0377) << 3) + 4, $2, 0, 0); }
|
	ARITHC HL ',' bcdehlsp
		{ if ($1->i_value == 1)
				emit(2,0355,0112+$4);
			else
				emit(2,0355,0102+$4);
		}
|
	ADD mar ',' bcdesp
		{ emitdad($2,$4); }
|
	ADD mar ',' mar
		{
			if ($2 != $4) {
				fprintf(stderr,"ADD mar, mar error\n");
				err[fflag]++;
			}
			emitdad($2,$4);
		}
|
	INCDEC evenreg
		{ emit1(($1->i_value << 3) + ($2 & 0377) + 3, $2, 0, 1); }
|
	PUSHPOP pushable
		{ emit1($1->i_value + ($2 & 0377), $2, 0, 1); }
|
	BIT expression ',' reg
		{
			if ($2 < 0 || $2 > 7)
				err[vflag]++;
			emit1($1->i_value + (($2 & 7) << 3) + ($4 & 0377), $4, 0, 0);
		}
|
	JP condition ',' expression
		{ emitjp(0302 + $2, $4); }
|
	JP '(' mar ')'
		{ emit1(0351, $3, 0, 1); }
|
	JP mar
		{ emit1(0351, $2, 0, 1); if (pass2) warnprt (1, 0); }
|
	CALL condition ',' expression
		{ emit(3, 0304 + $2, $4, $4 >> 8); }
|
	JR expression
		{ emitjr(030,$2); }
|
	JR spcondition ',' expression
		{ emitjr($1->i_value + $2, $4); }
|
	DJNZ expression
		{ emitjr($1->i_value, $2); }
|
	RET
		{ emit(1, $1->i_value); }
|
	RET condition
		{ emit(1, 0300 + $2); }
|
	LD reg ',' reg
		{
			if (($2 & 0377) == 6 && ($4 & 0377) == 6) {
				fprintf(stderr,"LD reg, reg error\n");
				err[fflag]++;
			}
			emit1(0100 + (($2 & 7) << 3) + ($4 & 7),$2 | $4, 0, 0);
		}
|
	LD reg ',' noparenexpr
		{
			if (suggest_optimise && pass2 && $4 == 0 && ($2 & 0377) == 7)
				warnprt (3, 0);
			emit1(6 + (($2 & 0377) << 3), $2, $4, 2);
		}
|
	LD reg ',' '(' RP ')'
		{	if ($2 != 7) {
				fprintf(stderr,"LD reg, (RP) error\n");
				err[fflag]++;
			}
			else emit(1, 012 + $5->i_value);
		}
|
	LD reg ',' parenexpr
		{
			if ($2 != 7) {
				fprintf(stderr,"LD reg, (expr) error\n");
				err[fflag]++;
			}
			else emit(3, 072, $4, $4 >> 8);
		}
|
	LD '(' RP ')' ',' ACC
		{ emit(1, 2 + $3->i_value); }
|
	LD parenexpr ',' ACC
		{ emit(3, 062, $2, $2 >> 8); }
|
	LD reg ',' MISCREG
		{
			if ($2 != 7) {
				fprintf(stderr,"LD reg, MISCREG error\n");
				err[fflag]++;
			}
			else emit(2, 0355, 0127 + $4->i_value);
		}
|
	LD MISCREG ',' ACC
		{ emit(2, 0355, 0107 + $2->i_value); }
|
	LD evenreg ',' lxexpression
		{ emit1(1 + ($2 & 060), $2, $4, 5); }
|
	LD evenreg ',' parenexpr
		{
			if (($2 & 060) == 040)
				emit1(052, $2, $4, 5);
			else
				emit(4, 0355, 0113 + $2, $4, $4 >> 8);
		}
|
	LD parenexpr ',' evenreg
		{
			if (($4 & 060) == 040)
				emit1(042, $4, $2, 5);
			else
				emit(4, 0355, 0103 + $4, $2, $2 >> 8);
		}
|
	LD evenreg ',' mar
		{
			if ($2 != 060) {
				fprintf(stderr,"LD evenreg error\n");
				err[fflag]++;
			}
			else
				emit1(0371, $4, 0, 1);
		}
|
	EX RP ',' HL
		{
			if ($2->i_value != 020) {
				fprintf(stderr,"EX RP, HL error\n");
				err[fflag]++;
			}
			else
				emit(1, 0353);
		}
|
	EX AF ',' AF setqf '\'' clrqf
		{ emit(1, 010); }
|
	EX '(' SP ')' ',' mar
		{ emit1(0343, $6, 0, 1); }
|
	IN realreg ',' parenexpr
		{
			if ($2 != 7) {
				fprintf(stderr,"IN reg, (expr) error\n");
				err[fflag]++;
			}
			else	{
				if ($4 < 0 || $4 > 255)
					err[vflag]++;
				emit(2, $1->i_value, $4);
			}
		}
|
	IN realreg ',' '(' C ')'
		{ emit(2, 0355, 0100 + ($2 << 3)); }
|
	IN F ',' '(' C ')'
		{ emit(2, 0355, 0160); }
|
	OUT parenexpr ',' ACC
		{
			if ($2 < 0 || $2 > 255)
				err[vflag]++;
			emit(2, $1->i_value, $2);
		}
|
	OUT '(' C ')' ',' realreg
		{ emit(2, 0355, 0101 + ($6 << 3)); }
|
	IM expression
		{
			if ($2 > 2 || $2 < 0)
				err[vflag]++;
			else
				emit(2, $1->i_value >> 8, $1->i_value + (($2 + ($2 > 0)) << 3));
		}
|
	PHASE expression
		{
			if (phaseflag) {
				err[oflag]++;
			} else {
				phaseflag = 1;
				phdollar = dollarsign;
				dollarsign = $2;
				phbegin = dollarsign;
			}
		}
|
	DEPHASE
		{
			if (!phaseflag) {
				err[oflag]++;
			} else {
				phaseflag = 0;
				dollarsign = phdollar + dollarsign - phbegin;
			}
		}
|
	ORG expression
		{
			if (not_seen_org)
				first_org_store=yyvsp[0].ival;
			not_seen_org=0;
			if (phaseflag) {
				err[oflag]++;
				dollarsign = phdollar + dollarsign - phbegin;
				phaseflag = 0;
			}
			if ($2-dollarsign) {
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
				olddollar = $2;
				dollarsign = $2;
			}
		}
|
	DEFB db.list
|
	DEFW dw.list
|
	ENDM
;


parm.list:
|
	parm.element
|
	parm.list ',' parm.element
;


parm.element:
	UNDECLARED
		{
			$1->i_token = MPARM;
			if (parm_number >= PARMMAX)
				error("Too many parameters");
			$1->i_value = parm_number++;
		}
;


arg.list:
	/* empty */
|
	arg.element
|
	arg.list ',' arg.element
;


arg.element:
	ARG
		{
			cp = malloc(strlen(tempbuf)+1);
#ifdef M_DEBUG
			fprintf (stderr, "[Arg%u(%p): %s]\n", parm_number, cp, tempbuf);
#endif
			est2[parm_number++] = cp;
			strcpy(cp, tempbuf);
		}
;
reg:
	realreg
|
	mem
;
realreg:
	REGNAME
		{
			$$ = $1->i_value;
		}
|
	ACC
		{
			$$ = $1->i_value;
		}
|
	C
		{
			$$ = $1->i_value;
		}
;
mem:
	'(' HL ')'
		{
			$$ = 6;
		}
|
	'(' INDEX expression ')'
		{
			disp = $3;
			$$ = ($2->i_value & 0177400) | 6;
		}
|
	'(' INDEX ')'
		{
			disp = 0;
			$$ = ($2->i_value & 0177400) | 6;
		}
;
evenreg:
	bcdesp
|
	mar
;
pushable:
	RP
		{
			$$ = $1->i_value;
		}
|
	AF
		{
			$$ = $1->i_value;
		}
|
	mar
;
bcdesp:
	RP
		{
			$$ = $1->i_value;
		}
|
	SP
		{
			$$ = $1->i_value;
		}
;
bcdehlsp:
	bcdesp
|
	HL
		{
			$$ = $1->i_value;
		}
;
mar:
	HL
		{
			$$ = $1->i_value;
		}
|
	INDEX
		{
			$$ = $1->i_value;
		}
;
condition:
	spcondition
|
	COND
		{
			$$ = $1->i_value;
		}
;
spcondition:
	SPCOND
		{
			$$ = $1->i_value;
		}
|
	C
		{	$$ = 030;	}
;
db.list:
	db.list.element
|
	db.list ',' db.list.element
;
db.list.element:
	TWOCHAR
		{
			dataemit(2, $1, $1>>8);
		}
|
	STRING
		{
			cp = $1;
			while (*cp != '\0')
				dataemit(1,*cp++);
		}
|
	expression
		{
			if ($1 < -128 || $1 > 255)
					err[vflag]++;
			dataemit(1, $1 & 0377);
		}
;


dw.list:
	dw.list.element
|
	dw.list ',' dw.list.element
;


dw.list.element:
	expression
		{
			dataemit(2, $1, $1>>8);
		}
;



lxexpression:
	noparenexpr
|
	TWOCHAR
;

expression:
	parenexpr
|
	noparenexpr
;

parenexpr:
	'(' expression ')'
		{	$$ = $2;	}
;

noparenexpr:
	LABEL
		{	$$ = $1->i_value; $1->i_uses++ ;	}
|
	NUMBER
|
	ONECHAR
|
	EQUATED
		{	$$ = $1->i_value; $1->i_uses++ ;	}
|
	WASEQUATED
		{
			$$ = $1->i_value; $1->i_uses++ ;
			if ($1->i_equbad) {
				/* forward reference to equ with a forward
				 * reference of its own cannot be resolved
				 * in two passes. -rjm
				 */
				err[frflag]++;
			}
		}
|
	DEFLED
		{	$$ = $1->i_value; $1->i_uses++ ;	}
|
	'$'
		{	$$ = dollarsign;	}
|
	UNDECLARED
		{
			err[uflag]++;
			equ_bad_label=1;
			$$ = 0;
		}
|
	MULTDEF
		{	$$ = $1->i_value;	}
|
	expression '+' expression
		{	$$ = $1 + $3;	}
|
	expression '-' expression
		{	$$ = $1 - $3;	}
|
	expression '/' expression
		{	if ($3 == 0) err[eflag]++; else $$ = $1 / $3;	}
|
	expression '*' expression
		{	$$ = $1 * $3;	}
|
	expression '%' expression
		{	if ($3 == 0) err[eflag]++; else $$ = $1 % $3;	}
|
	expression MOD expression
		{	if ($3 == 0) err[eflag]++; else $$ = $1 % $3;	}
|
	expression '&' expression
		{	$$ = $1 & $3;	}
|
	expression AND expression
		{	$$ = $1 & $3;	}
|
	expression '|' expression
		{	$$ = $1 | $3;	}
|
	expression OR expression
		{	$$ = $1 | $3;	}
|
	expression '^' expression
		{	$$ = $1 ^ $3;	}
|
	expression XOR expression
		{	$$ = $1 ^ $3;	}
|
	expression SHL expression
		{	$$ = $1 << $3;	}
|
	expression SHR expression
		{	$$ = (($1 >> 1) & 077777) >> ($3 - 1);	}
|
	expression '<' expression
		{	$$ = $1 < $3;	}
|
	expression '=' expression
		{	$$ = $1 == $3;	}
|
	expression '>' expression
		{	$$ = $1 > $3;	}
|
	expression LT expression
		{	$$ = $1 < $3;	}
|
	expression EQ expression
		{	$$ = $1 == $3;	}
|
	expression GT expression
		{	$$ = $1 > $3;	}
|
	expression LE expression
		{	$$ = $1 <= $3;	}
|
	expression GE expression
		{	$$ = $1 >= $3;	}
|
	expression NE expression
		{	$$ = $1 != $3;	}
|
	'[' expression ']'
		{	$$ = $2;	}
|
	NOT expression
		{	$$ = ~$2;	}
|
	'~' expression
		{	$$ = ~$2;	}
|
	'!' expression
		{	$$ = !$2;	}
|
	'+' expression %prec UNARY
		{	$$ = $2;	}
|
	'-' expression %prec UNARY
		{	$$ = -$2;	}
;

symbol:
	UNDECLARED
|
	LABEL
|
	MULTDEF
|
	EQUATED
|
	WASEQUATED
|
	DEFLED
;


al:
	{ int i;
		if (expptr >= MAXEXP)
			error("Macro expansion level");
		est2 = (char **) malloc((PARMMAX +4) * sizeof(char *));
		expstack[expptr] = (char *)est2 ;
		for (i=0; i<PARMMAX; i++)
			est2[i] = 0;
		arg_flag++;
	}
;


arg_on:
	{	arg_flag++;	}
;

arg_off:
		{	arg_flag = 0;	}
;

setqf:
		{	quoteflag++;	}
;

clrqf:
		{	quoteflag = 0;	}

;

%%

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
