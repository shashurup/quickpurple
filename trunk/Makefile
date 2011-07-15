all: quickpurple.la

quickpurple.lo: quickpurple.c
	libtool --mode=compile gcc -g -shared $(shell pkg-config --cflags pidgin gtkhotkey-1.0) -c quickpurple.c

quickpurple.la: quickpurple.lo
	libtool --mode=link gcc -g -shared -module -avoid-version -rpath $(shell pkg-config --variable=plugindir pidgin) $(shell pkg-config --libs pidgin gtkhotkey-1.0) -o quickpurple.la quickpurple.lo

clean:
	libtool --mode=clean rm quickpurple.la quickpurple.lo

install:
	install -D .libs/quickpurple.so $(DESTDIR)$(shell pkg-config --variable=plugindir pidgin)/quickpurple.so
