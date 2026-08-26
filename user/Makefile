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

# C files which are libraries/support files, not executables.
SUPPORT_SOURCES := runtime.c

# Every other .c file in this directory becomes a .nex executable.
PROGRAM_SOURCES := $(filter-out $(SUPPORT_SOURCES),$(wildcard *.c))
PROGRAMS := $(PROGRAM_SOURCES:.c=.nex)
PROGRAM_OBJS := $(PROGRAM_SOURCES:.c=.o)

.PHONY: all clean install

all: $(PROGRAMS)

# Build every userspace executable.
%.nex: %.o crt0.o runtime.o linker.ld
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(LDFLAGS) $(USER_LDFLAGS) \
		-o $@ crt0.o runtime.o $<

# Compile program source files.
%.o: %.c runtime.h
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) -c $< -o $@

runtime.o: runtime.c runtime.h
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) -c $< -o $@

crt0.o: crt0.S
	$(CC) $(CFLAGS) $(USER_CFLAGS) $(CPPFLAGS) -c $< -o $@

install: $(PROGRAMS)
	mkdir -p "$(DESTDIR)$(BINDIR)"
	for program in $(PROGRAMS); do \
		echo "Installing $$program -> $(DESTDIR)$(BINDIR)/$$program"; \
		cp "$$program" "$(DESTDIR)$(BINDIR)/$$program"; \
	done

clean:
	rm -f $(PROGRAMS) $(PROGRAM_OBJS) runtime.o crt0.o