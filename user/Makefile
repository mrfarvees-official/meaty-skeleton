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

HELLO_OBJS=\
	crt0.o \
	runtime.o \
	hello.o

SPAWN_CHILD_OBJS=\
	crt0.o \
	spawn_child.o

.PHONY: all clean install

all: hello.nex spawn-child.nex

hello.nex: $(HELLO_OBJS) linker.ld
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(LDFLAGS) $(USER_LDFLAGS) -o $@ $(HELLO_OBJS)

spawn-child.nex: $(SPAWN_CHILD_OBJS) linker.ld
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(LDFLAGS) $(USER_LDFLAGS) -o $@ $(SPAWN_CHILD_OBJS)

crt0.o: crt0.S
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) -c $< -o $@

runtime.o: runtime.c runtime.h
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) -c runtime.c -o $@

hello.o: hello.c runtime.h
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) -c hello.c -o $@

spawn_child.o: spawn_child.c
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) -c spawn_child.c -o $@

install: hello.nex spawn-child.nex
	mkdir -p $(DESTDIR)$(BINDIR)
	cp hello.nex $(DESTDIR)$(BINDIR)/hello.nex
	cp spawn-child.nex $(DESTDIR)$(BINDIR)/spawn-child.nex

clean:
	rm -f hello.nex spawn-child.nex \
		hello.o spawn_child.o runtime.o crt0.o