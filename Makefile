target  ?= efsextract
objects := asprintf.o efsextract.o efs.o hexdump.o pdscan.o progname.o tar.o

libs:=libiso9660

EXTRAS = -fsanitize=bounds -fsanitize=undefined -fsanitize=null -fcf-protection=full -fstack-protector-all -fstack-check -Wimplicit-fallthrough -Wall -Wc90-c99-compat

#ifdef libs
#LDLIBS += $(shell pkg-config --libs   ${libs})
#CFLAGS += $(shell pkg-config --cflags ${libs})
#endif

LDLIBS += -liso9660 -lcdio -lm

LDFLAGS += ${EXTRAS}
CFLAGS  = -std=gnu99 -Wall -ggdb ${EXTRAS}

.PHONY: all
all:	$(target) README

.PHONY: clean
clean:
	rm -f $(target) $(objects)

.PHONY: install
install: ${target} ${target}.1
	install -m 755 ${target} /usr/local/bin
	install -m 755 -d /usr/local/share/man/man1
	install -m 644 ${target}.1 /usr/local/share/man/man1

.PHONY: uninstall
uninstall:
	rm -f /usr/local/bin/${target} /usr/local/share/man/man1/${target}.1

README: ${target}.1
	MANWIDTH=77 man --nh --nj ./${target}.1 | col -b > $@

$(target): $(objects)
