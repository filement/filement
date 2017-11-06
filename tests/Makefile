DEF=-D_GNU_SOURCE -DOS_BSD -D_BSD_SOURCE -D_FILE_OFFSET_BITS=64 -DPLATFORM_ID=2 -DOS_MAC -DTLS -DEXECUTABLE=\"/Applications/Filement.app/Contents/MacOS/Filement\" -DPREFIX=\"/Applications/Filement.app/\" -DDEVICE
INC=-I$(CURDIR) -I$(CURDIR)/lib -I$(CURDIR)/../include
LIB=-L/opt/lib
WARNINGS=-Werror -Wchar-subscripts -Wimplicit -Wsequence-point -Wcast-align -Wstrict-aliasing -Wno-parentheses -Wno-comment -Wno-pointer-sign -Wno-return-type
#WARNINGS=-Werror -Wchar-subscripts -Wimplicit -Wsequence-point -Wcast-align -Wstrict-aliasing -Wno-parentheses -Wno-empty-body -Wno-comment

export CC=gcc
export CFLAGS=-std=c99 -fPIC -pthread -DRUN_MODE=1 -g -DDEBUG $(DEF) $(INC) $(WARNINGS) -Wno-unused-value
export LDFLAGS=-pthread $(LIB) -lz -lcheck

check: format.o lib/format.o
	$(CC) $^ $(LDFLAGS) -o format
	./format

clean:
	rm -f *.o
	rm -f format
