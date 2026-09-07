# dshuf - suckless multi-key balanced shuffler
# See LICENSE file for copyright and license details.

include config.mk

SRC = dshuf.c
OBJ = $(SRC:.c=.o)

all: options dshuf libdshuf.so

options:
	@echo dshuf build options:
	@echo "CFLAGS   = $(CFLAGS)"
	@echo "LDFLAGS  = $(LDFLAGS)"
	@echo "CC       = $(CC)"

.c.o:
	@echo CC $<
	@$(CC) -c $(CFLAGS) $<

$(OBJ): config.mk arg.h dshuf.h

dshuf: $(OBJ)
	@echo CC -o $@
	@$(CC) -o $@ $(OBJ) $(LDFLAGS)

libdshuf.so: dshuf_lib.c dshuf.h
	@echo CC -shared -o $@
	@$(CC) $(CFLAGS) -fPIC -shared dshuf_lib.c -o $@

clean:
	@echo cleaning
	@rm -f dshuf $(OBJ) dshuf-$(VERSION).tar.gz tests/test_dshuf tests/test_cpp libdshuf.so

dist: clean
	@echo creating dist tarball
	@mkdir -p dshuf-$(VERSION)/tests dshuf-$(VERSION)/bindings
	@cp -R LICENSE Makefile README.md config.mk dshuf.1 arg.h dshuf.h dshuf.c dshuf_lib.c tests bindings dshuf-$(VERSION)
	@tar -cf dshuf-$(VERSION).tar dshuf-$(VERSION)
	@gzip dshuf-$(VERSION).tar
	@rm -rf dshuf-$(VERSION)

install: all
	@echo installing executable file to $(DESTDIR)$(PREFIX)/bin
	@mkdir -p $(DESTDIR)$(PREFIX)/bin
	@cp -f dshuf $(DESTDIR)$(PREFIX)/bin
	@chmod 755 $(DESTDIR)$(PREFIX)/bin/dshuf
	@echo installing manual page to $(DESTDIR)$(MANPREFIX)/man1
	@mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	@sed "s/VERSION/$(VERSION)/g" < dshuf.1 > $(DESTDIR)$(MANPREFIX)/man1/dshuf.1
	@chmod 644 $(DESTDIR)$(MANPREFIX)/man1/dshuf.1

uninstall:
	@echo removing executable file from $(DESTDIR)$(PREFIX)/bin
	@rm -f $(DESTDIR)$(PREFIX)/bin/dshuf
	@echo removing manual page from $(DESTDIR)$(MANPREFIX)/man1
	@rm -f $(DESTDIR)$(MANPREFIX)/man1/dshuf.1

test: dshuf
	@echo running C unit tests
	@$(CC) $(CFLAGS) tests/test_dshuf.c -o tests/test_dshuf $(LDFLAGS)
	@./tests/test_dshuf
	@echo running CLI pipeline tests
	@sh tests/test_cli.sh

sanitize: clean
	@echo compiling with AddressSanitizer and UndefinedBehaviorSanitizer
	@$(CC) $(CFLAGS) -g -fsanitize=address,undefined dshuf.c -o dshuf $(LDFLAGS) -fsanitize=address,undefined
	@$(CC) $(CFLAGS) -g -fsanitize=address,undefined tests/test_dshuf.c -o tests/test_dshuf $(LDFLAGS) -fsanitize=address,undefined
	@./tests/test_dshuf
	@sh tests/test_cli.sh

test-cpp:
	@echo running C++ binding tests
	@$${CXX:-c++} -std=c++11 -Wall -Wextra -pedantic -O2 tests/test_cpp.cpp -o tests/test_cpp
	@./tests/test_cpp
	@rm -f tests/test_cpp

test-python: libdshuf.so
	@echo running Python binding tests
	@python3 tests/test_python.py

test-all: test test-cpp test-python

.PHONY: all options clean dist install uninstall test sanitize test-cpp test-python test-all