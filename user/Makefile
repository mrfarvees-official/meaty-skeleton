CFLAGS?=-O2 -g
CPPFLAGS?=
LDFLAGS?=

DESTDIR?=
PREFIX?=/usr
EXEC_PREFIX?=$(PREFIX)
BINDIR?=/bin

USER_CFLAGS=\
	-std=gnu11 \
	-ffreestanding \
	-fno-stack-protector \
	-fno-pie \
	-fno-pic \
	-fno-asynchronous-unwind-tables \
	-fno-unwind-tables \
	-Wall \
	-Wextra \
	-m32

USER_LDFLAGS=\
	-nostdlib \
	-no-pie \
	-Wl,--build-id=none \
	-Wl,-z,max-page-size=0x1000 \
	-Wl,-T,linker.ld

HELLO_OBJS=crt0.o runtime.o hello.o
SPAWN_CHILD_OBJS=crt0.o spawn_child.o
SH_OBJS=crt0.o runtime.o sh.o
ECHO_OBJS=crt0.o runtime.o echo.o
CLEAR_OBJS=crt0.o runtime.o clear.o
CAT_OBJS=crt0.o runtime.o cat.o
TOUCH_OBJS=crt0.o runtime.o touch.o

PROGRAMS=\
	hello.nex \
	spawn-child.nex \
	sh.nex \
	echo.nex \
	clear.nex \
	cat.nex \
	touch.nex

.PHONY: all clean install

all: $(PROGRAMS)

hello.nex: $(HELLO_OBJS) linker.ld
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(LDFLAGS) $(USER_LDFLAGS) -o $@ $(HELLO_OBJS)

spawn-child.nex: $(SPAWN_CHILD_OBJS) linker.ld
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(LDFLAGS) $(USER_LDFLAGS) -o $@ $(SPAWN_CHILD_OBJS)

sh.nex: $(SH_OBJS) linker.ld
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(LDFLAGS) $(USER_LDFLAGS) -o $@ $(SH_OBJS)

echo.nex: $(ECHO_OBJS) linker.ld
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(LDFLAGS) $(USER_LDFLAGS) -o $@ $(ECHO_OBJS)

clear.nex: $(CLEAR_OBJS) linker.ld
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(LDFLAGS) $(USER_LDFLAGS) -o $@ $(CLEAR_OBJS)

cat.nex: $(CAT_OBJS) linker.ld
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(LDFLAGS) $(USER_LDFLAGS) -o $@ $(CAT_OBJS)

touch.nex: $(TOUCH_OBJS) linker.ld
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(LDFLAGS) $(USER_LDFLAGS) -o $@ $(TOUCH_OBJS)

crt0.o: crt0.S
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) -c $< -o $@

runtime.o: runtime.c runtime.h
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) -c runtime.c -o $@

hello.o: hello.c runtime.h
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) -c hello.c -o $@

spawn_child.o: spawn_child.c
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) -c spawn_child.c -o $@

sh.o: sh.c runtime.h
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) -c sh.c -o $@

echo.o: echo.c runtime.h
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) -c echo.c -o $@

clear.o: clear.c runtime.h
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) -c clear.c -o $@

cat.o: cat.c runtime.h
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) -c cat.c -o $@

touch.o: touch.c runtime.h
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) -c touch.c -o $@

install: $(PROGRAMS)
	mkdir -p $(DESTDIR)$(BINDIR)
	for program in $(PROGRAMS); do \
		cp $$program $(DESTDIR)$(BINDIR)/$$program; \
	done

clean:
	rm -f \
		$(PROGRAMS) \
		hello.o \
		spawn_child.o \
		sh.o \
		echo.o \
		clear.o \
		cat.o \
		touch.o \
		runtime.o \
		crt0.o