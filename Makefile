CC = gcc
X = .exe
prefix = /usr/local
exec_prefix = ${prefix}
bindir = ${exec_prefix}/bin

VERSION = 0.1.1

msi_tool_SOURCES = \
	msi-tool.c colon-parser.c colon-parser.h \
	bool.h exparray.h xmalloc.c xmalloc.h

DISTFILES = $(msi_tool_SOURCES) \
	README.md COPYING howto.md Makefile exparray.gdb

all: msi-tool$(X)

msi-tool$(X): msi-tool.c colon-parser.c colon-parser.h
	$(CC) -g -o $@ msi-tool.c colon-parser.c xmalloc.c

clean:
	rm -f msi-tool$(X)

install:
	install msi-tool$(X) $(bindir)/msi-tool$(X)

uninstall:
	rm -f $(bindir)/msi-tool$(X)

dist:
	mkdir msitool-$(VERSION)
	cp -p $(DISTFILES) msitool-$(VERSION)
	zip -9rq msitool-$(VERSION).zip msitool-$(VERSION)
	rm -rf msitool-$(VERSION)
