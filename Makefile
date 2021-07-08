REV=$(shell sh -c 'date +"%Y.%m.%d"')

all: ogg-winmm.dll

ogg-winmm.rc.o: ogg-winmm.rc.in
	sed 's/__REV__/$(REV)/' ogg-winmm.rc.in | windres -O coff -o ogg-winmm.rc.o

ogg-winmm.dll: ogg-winmm.c ogg-winmm.rc.o ogg-winmm.def player.c stubs.c
	mingw32-gcc -std=gnu99 -static-libgcc -Wl,--enable-stdcall-fixup -s -O2 -shared -o ogg-winmm.dll ogg-winmm.c player.c stubs.c ogg-winmm.def ogg-winmm.rc.o -Ilibs/include -Llibs -lwinmm -l:libvorbisfile.a -l:libvorbis.a -l:libogg.a

clean:
	rm -f ogg-winmm.dll ogg-winmm.rc.o
