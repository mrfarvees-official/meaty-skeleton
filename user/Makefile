CFLAGS?=-O2 -g
CPPFLAGS?=
LDFLAGS?=

DESTDIR?=
PREFIX?=/usr
EXEC_PREFIX?=$(PREFIX)
BINDIR?=$(EXEC_PREFIX)/bin

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

OBJS=\
	crt0.o \
	hello.o

.PHONY: all clean install

all: hello.elf

hello.elf: $(OBJS) linker.ld
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(LDFLAGS) $(USER_LDFLAGS) -o $@ $(OBJS)

crt0.o: crt0.S
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) -c $< -o $@

hello.o: hello.c
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) -c $< -o $@

install: hello.elf
	mkdir -p $(DESTDIR)$(BINDIR)
	cp hello.elf $(DESTDIR)$(BINDIR)/hello

clean:
	rm -f hello.elf $(OBJS)