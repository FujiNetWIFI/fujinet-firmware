/* prototypes for mio.h */

extern FILE *mfopen(char *filename,char *mode);
extern int mfclose(FILE *f);
extern int mfputc(unsigned int c,FILE *f);
extern int mfgetc(FILE *f);
extern int mfseek(FILE *f,long loc,int origin);
extern int mfread(char *ptr, unsigned int size, unsigned int nitems, FILE *f);
extern int mfwrite(char *ptr, int size, int nitems, FILE *f);
