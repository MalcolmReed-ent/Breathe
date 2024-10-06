CC = gcc
CFLAGS = -Wall -Wextra -O2 `pkg-config --cflags poppler-glib x11 cairo`
LDFLAGS = `pkg-config --libs poppler-glib x11 cairo` -lm
DEPS = coordconv.h rectangle.h config.h
OBJ = main.o coordconv.o rectangle.o

PREFIX ?= /usr/local
MANPREFIX ?= $(PREFIX)/share/man

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

breathe: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

.PHONY: clean install uninstall

clean:
	rm -f *.o breathe

install: breathe
	install -D -m 755 breathe $(DESTDIR)$(PREFIX)/bin/breathe
	install -D -m 644 breathe.1 $(DESTDIR)$(MANPREFIX)/man1/breathe.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/breathe
	rm -f $(DESTDIR)$(MANPREFIX)/man1/breathe.1
