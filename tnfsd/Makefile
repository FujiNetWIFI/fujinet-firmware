CC=gcc

ifndef OS
    $(error Must specify an OS with make OS=... where OS is LINUX, BSD or WINDOWS. Use BSD for MacOSX)
endif

ifeq ($(OS),LINUX)
    FLAGS = -Wall -DUNIX -DNEED_BSDCOMPAT -DENABLE_CHROOT -DNEED_ERRTABLE
    EXOBJS = strlcpy.o strlcat.o
    LIBS =
    EXEC = tnfsd
endif
ifeq ($(OS),WINDOWS)
    FLAGS = -Wall -DWIN32 -DNEED_ERRTABLE -DNEED_BSDCOMPAT
    EXOBJS = strlcpy.o strlcat.o
    LIBS = -lwsock32
    EXEC = tnfsd.exe
endif
ifeq ($(OS),BSD)
    FLAGS = -Wall -DUNIX -DENABLE_CHROOT -DNEED_ERRTABLE
    EXOBJS =
    LIBS =
    EXEC = tnfsd
endif

ifdef DEBUG
    EXFLAGS = -g -DDEBUG
endif
    
CFLAGS=$(FLAGS) $(EXFLAGS)
OBJS=main.o datagram.o log.o session.o endian.o directory.o errortable.o tnfs_file.o chroot.o $(EXOBJS)

all:	$(OBJS)
	$(CC) -o $(EXEC) $(OBJS) $(LIBS)

clean:
	$(RM) -f $(OBJS) $(EXEC)

