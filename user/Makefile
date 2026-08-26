CFLAGS ?= -O2 -g
CPPFLAGS ?=
LDFLAGS ?=

DESTDIR ?=
PREFIX ?= /usr
EXEC_PREFIX ?= $(PREFIX)
BINDIR ?= /bin

USER_CFLAGS = \
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

USER_LDFLAGS = \
	-nostdlib \
	-no-pie \
	-Wl,--build-id=none \
	-Wl,-z,max-page-size=0x1000 \
	-Wl,-T,linker.ld

#
# Shared userspace support sources.
#
SUPPORT_SOURCES := \
	runtime.c \
	app_descriptor.c \
	link_descriptor.c

SUPPORT_OBJS := \
	$(SUPPORT_SOURCES:.c=.o)

#
# Every other C source becomes a userspace executable.
#
PROGRAM_SOURCES := \
	$(filter-out $(SUPPORT_SOURCES),$(wildcard *.c))

PROGRAMS := \
	$(PROGRAM_SOURCES:.c=.nex)

PROGRAM_OBJS := \
	$(PROGRAM_SOURCES:.c=.o)

.PHONY: all clean install

all: $(PROGRAMS)

#
# appinfo uses the application descriptor parser.
#
appinfo.nex: appinfo.o app_descriptor.o crt0.o runtime.o linker.ld
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(LDFLAGS) $(USER_LDFLAGS) \
		-o $@ \
		crt0.o \
		runtime.o \
		app_descriptor.o \
		appinfo.o

#
# linkinfo resolves both link and application descriptors.
#
linkinfo.nex: linkinfo.o link_descriptor.o app_descriptor.o crt0.o runtime.o linker.ld
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(LDFLAGS) $(USER_LDFLAGS) \
		-o $@ \
		crt0.o \
		runtime.o \
		link_descriptor.o \
		app_descriptor.o \
		linkinfo.o

#
# Ordinary programs need only crt0 + runtime.
#
%.nex: %.o crt0.o runtime.o linker.ld
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(LDFLAGS) $(USER_LDFLAGS) \
		-o $@ \
		crt0.o \
		runtime.o \
		$<

#
# Generic C compilation.
#
%.o: %.c runtime.h
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) \
		-c $< \
		-o $@

app_descriptor.o: app_descriptor.c app_descriptor.h runtime.h
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) \
		-c app_descriptor.c \
		-o app_descriptor.o

link_descriptor.o: link_descriptor.c link_descriptor.h runtime.h
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) \
		-c link_descriptor.c \
		-o link_descriptor.o

appinfo.o: appinfo.c app_descriptor.h runtime.h
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) \
		-c appinfo.c \
		-o appinfo.o

linkinfo.o: linkinfo.c link_descriptor.h app_descriptor.h runtime.h
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) \
		-c linkinfo.c \
		-o linkinfo.o

runtime.o: runtime.c runtime.h
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) \
		-c runtime.c \
		-o runtime.o

crt0.o: crt0.S
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) \
		-c crt0.S \
		-o crt0.o

install: $(PROGRAMS)
	mkdir -p "$(DESTDIR)$(BINDIR)"
	for program in $(PROGRAMS); do \
		echo "Installing $$program -> $(DESTDIR)$(BINDIR)/$$program"; \
		cp "$$program" "$(DESTDIR)$(BINDIR)/$$program"; \
	done

clean:
	rm -f \
		$(PROGRAMS) \
		$(PROGRAM_OBJS) \
		$(SUPPORT_OBJS) \
		crt0.o