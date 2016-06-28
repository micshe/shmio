CC=cc
CFLAGS=-Wall -Wextra
AR=ar
PREFIX=/usr/local
DESTDIR=

help:
#FIXME add detailed help section

options:
	@echo "CC = $(CC)"
	@echo "PREFIX = $(PREFIX)"
	@echo "DESTDIR = $(DESTDIR)"

libshmio.a: shmio.c shmio.h
	$(CC) -c shmio.c -o shmio.o $(CFLAGS)
	$(AR) rcs libshmio.a shmio.o

libshmio.so: shmio.c shmio.h
	$(CC) -shared shmio.c -o libshmio.so $(CFLAGS)

shmio-hlib.h: shmio.c shmio.h
	@echo "#ifndef SYSIO_SHMIO_HLIB" > shmio-hlib.h
	@echo "#define SYSIO_SHMIO_HLIB" >> shmio-hlib.h
	@cat shmio.h >> shmio-hlib.h
	@cat shmio.c >> shmio-hlib.h
	@echo "#endif" >> shmio-hlib.h

test: test.c shmio.c shmio.h
	$(CC) -o test test.c shmio.c $(CFLAGS) 
	./test

static: libshmio.a

shared: libshmio.so

hlib: shmio-hlib.h

all: static shared hlib 

shmio: all

clean:
	@rm -f test
	@rm -f shmio.o
	@rm -f libshmio.a
	@rm -f libshmio.so
	@rm -f shmio-hlib.h

install-header: shmio.h
	@echo "installing shmio.h to $(DESTDIR)$(PREFIX)/include"
	@mkdir -p $(DESTDIR)$(PREFIX)/include
	@cp shmio.h $(DESTDIR)$(PREFIX)/include/shmio.h

install-hlib: shmio-hlib.h
	@echo "installing shmio-hlib to $(DESTDIR)$(PREFIX)/include"
	@mkdir -p $(DESTDIR)$(PREFIX)/include
	@cp shmio-hlib.h $(DESTDIR)$(PREFIX)/include/shmio-hlib.h
#FIXME add 'to use...' explanation

install-static: install-header libshmio.a
	@echo "installing libshmio.a to $(DESTDIR)$(PREFIX)/lib"
	@mkdir -p $(DESTDIR)$(PREFIX)/lib
	@cp libshmio.a $(DESTDIR)$(PREFIX)/lib/libshmio.a
#FIXME add 'to use...' explanation

install-shared: install-header libshmio.so
	@echo "installing libshmio.iso to $(DESTDIR)$(PREFIX)/lib"
	@mkdir -p $(DESTDIR)$(PREFIX)/lib
	@cp libshmio.so $(DESTDIR)$(PREFIX)/lib/libshmio.so
#FIXME add 'to use...' explanation

install: install-header install-hlib install-static install-shared

uninstall-header:
	@echo "uninstalling shmio.h from $(DESTDIR)$(PREFIX)/include"
	@rm -f $(DESTDIR)$(PREFIX)/include/shmio.h

uninstall-hlib:
	@echo "uninstalling shmio-hlib.h from $(DESTDIR)$(PREFIX)/include"
	@rm -f $(DESTDIR)$(PREFIX)/include/shmio-hlib.h

uninstall-static:
	@echo "uninstalling libshmio.a from $(DESTDIR)$(PREFIX)/lib"
	@rm -f $(DESTDIR)$(PREFIX)/lib/libshmio.a

uninstall-shared:
	@echo "uninstalling libshmio.so from $(DESTDIR)$(PREFIX)/lib"
	@rm -f $(DESTDIR)$(PREFIX)/lib/libshmio.so

uninstall: uninstall-header uninstall-hlib uninstall-static uninstall-shared

.PHONY: shmio clean hlib shared static all test options help \
install-header install-hlib install-static install-shared install \
uninstall-header uninstall-hlib uninstall-static uninstall-shared uninstall \

