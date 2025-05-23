REV=$(shell sh -c 'date +"%Y,%m,%d"')

all: wav-winmm.dll

wav-winmm.rc.o: wav-winmm.rc.in
	sed 's/__REV__/$(REV)/' wav-winmm.rc.in | windres -O coff -o wav-winmm.rc.o

wav-winmm.dll: wav-winmm.c player.c player.h stubs.c stub.h wav-winmm.def wav-winmm.rc.o
	gcc -m32 -std=gnu99 -static-libgcc -Wl,--enable-stdcall-fixup,--gc-sections -s -O2 -shared -o winmm.dll wav-winmm.c player.c stubs.c wav-winmm.def wav-winmm.rc.o -lwinmm

clean:
	rm -f winmm.dll wav-winmm.rc.o
