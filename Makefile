REV=$(shell sh -c 'date +"%Y,%m,%d"')

OBJECTS = ogg-winmm.o player.o hookshot_stubs.o hookshot_entry.o ogg-winmm.rc.o

all: ogg-winmm.HookModule.32.dll

ogg-winmm.rc.o: ogg-winmm.rc.in
	sed 's/__REV__/$(REV)/' ogg-winmm.rc.in | windres -O coff -o ogg-winmm.rc.o

%.o: %.c
	gcc -m32 -std=gnu99 -O2 -I./Hookshot/Include/Hookshot -c -o $@ $<

%.o: %.cpp
	g++ -m32 -O2 -I./Hookshot/Include/Hookshot -c -o $@ $<

ogg-winmm.HookModule.32.dll: $(OBJECTS) player.h stub.h
	g++ -m32 -static-libgcc -static-libstdc++ -Wl,--enable-stdcall-fixup,--gc-sections -s -shared -o $@ $(OBJECTS) -lwinmm -Wl,-Bstatic -lvorbisfile -lvorbis -logg -Wl,-Bdynamic
	upx --best $@

clean:
	rm -f $(OBJECTS) ogg-winmm.HookModule.32.dll