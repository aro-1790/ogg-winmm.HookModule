# Version stamp written by version.sh, used verbatim as the comma-separated resource form
REV=$(shell sed -e 's/^v//' -e 's/\./,/g' resource/version.txt)

# Guard against a missing/empty version file (run ./version.sh first)
CHECK_REV := test -n "$(REV)" || { echo "resource/version.txt is empty - run ./version.sh first" >&2; exit 1; }

OBJECTS = ogg-winmm.o player.o hookshot_stubs.o hookshot_entry.o ogg-winmm.rc.o

all: ogg-winmm.HookModule.32.dll

ogg-winmm.rc.o: ogg-winmm.rc.in resource/version.txt
	@$(CHECK_REV)
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