REV=$(shell sh -c 'date +"%Y,%m,%d"')

all: ogg-winmm.dll

ogg-winmm.rc.o: ogg-winmm.rc.in
	sed 's/__REV__/$(REV)/' ogg-winmm.rc.in | windres -O coff -o ogg-winmm.rc.o

ogg-winmm.dll: ogg-winmm.c player.c player.h stubs.c stub.h ogg-winmm.def ogg-winmm.rc.o
	gcc -m32 -std=gnu99 -static-libgcc -mcrtdll=msvcrt40 -Wl,--enable-stdcall-fixup,--gc-sections -s -O2 -shared -o winmm.dll ogg-winmm.c player.c stubs.c ogg-winmm.def ogg-winmm.rc.o -Ilibs/include -Llibs -lwinmm -l:libvorbisfile.a -l:libvorbis.a -l:libogg.a

clean:
	rm -f winmm.dll ogg-winmm.rc.o
