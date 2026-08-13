DEFAULT_HOST!=../default-host.sh
HOST?=DEFAULT_HOST
HOSTARCH!=../target-triplet-to-arch.sh $(HOST)

CFLAGS?=-O2 -g
CPPFLAGS?=
CXXFLAGS?=-O2 -g
LDFLAGS?=
LIBS?=

DESTDIR?=
PREFIX?=/usr/local
EXEC_PREFIX?=$(PREFIX)
BOOTDIR?=$(EXEC_PREFIX)/boot
INCLUDEDIR?=$(PREFIX)/include

CFLAGS:=$(CFLAGS) -ffreestanding -fstack-protector-strong -Wall -Wextra
CPPFLAGS:=$(CPPFLAGS) -D__is_kernel -Iinclude
CXXFLAGS:=$(CXXFLAGS) -ffreestanding -Wall -Wextra -fno-exceptions -fno-rtti
LDFLAGS:=$(LDFLAGS)
LIBS:=$(LIBS) -nostdlib -lk -lgcc

ARCHDIR=arch/$(HOSTARCH)

include $(ARCHDIR)/make.config

CFLAGS:=$(CFLAGS) $(KERNEL_ARCH_CFLAGS)
CPPFLAGS:=$(CPPFLAGS) $(KERNEL_ARCH_CPPFLAGS)
LDFLAGS:=$(LDFLAGS) $(KERNEL_ARCH_LDFLAGS)
LIBS:=$(LIBS) $(KERNEL_ARCH_LIBS)

KERNEL_OBJS=\
$(KERNEL_ARCH_OBJS) \
kernel/kernel.o \
kernel/test/test.o \
kernel/test/suite.o \
kernel/test/pmm_stress.o \
kernel/test/paging_stress.o \
kernel/test/heap_stress.o \
kernel/test/mutex_stress.o \
kernel/test/semaphore_stress.o \
kernel/test/task_cleanup_stress.o \
kernel/test/keyboard.o \
kernel/test/keyboard_event.o \
kernel/test/keyboard_blocking.o \
kernel/test/keyboard_line.o \
kernel/test/cpu_detection.o \
kernel/test/smp_multitasking_stress.o \
kernel/test/ramfs.o \
kernel/test/ata.o \
kernel/test/partition.o \
kernel/memory/pmm.o \
kernel/memory/paging.o \
kernel/memory/heap.o \
kernel/memory/spinlock.o \
kernel/process/process.o \
kernel/task/task.o \
kernel/task/mutex.o \
kernel/task/semaphore.o \
kernel/task/wait_queue.o \
kernel/task/sleep_queue.o \
kernel/scheduler/scheduler.o \
kernel/scheduler/round_robin.o \
kernel/device/keyboard.o \
kernel/device/pci.o \
kernel/device/ahci.o \
kernel/input/keyboard_line.o \
kernel/multitasking/acpi.o \
kernel/multitasking/smp.o \
kernel/multitasking/cpu.o \
kernel/fs/vfs.o \
kernel/fs/ramfs.o \
kernel/fs/ata.o \
kernel/fs/block_device.o \
kernel/fs/partition.o \
kernel/fs/ext2.o \
kernel/system/system_info.o \
kernel/timer/timer.o

OBJS=\
	$(ARCHDIR)/crti.o \
	$(ARCHDIR)/crtbegin.o \
	$(KERNEL_OBJS) \
	$(ARCHDIR)/crtend.o \
	$(ARCHDIR)/crtn.o 

LINK_LIST=\
	$(LDFLAGS) \
	$(ARCHDIR)/crti.o \
	$(ARCHDIR)/crtbegin.o \
	$(KERNEL_OBJS) \
	$(LIBS) \
	$(ARCHDIR)/crtend.o \
	$(ARCHDIR)/crtn.o 

.PHONY: all clean install install-headers install-kernel
.SUFFIXES: .o .c .cpp .S

all: myos.kernel

myos.kernel: $(OBJS) $(ARCHDIR)/linker.ld
	$(CC) -T $(ARCHDIR)/linker.ld -o $@ $(CFLAGS) $(LINK_LIST)
	grub-file --is-x86-multiboot myos.kernel

$(ARCHDIR)/crtbegin.o $(ARCHDIR)/crtend.o:
	OBJ=`$(CC) $(CFLAGS) $(LDFLAGS) -print-file-name=$(@F)` && cp "$$OBJ" $@

kernel/kernel.o: CFLAGS += -O0 -fstack-protector-all
kernel/panic.o: CFLAGS += -fno-stack-protector
kernel/ssp.o: CFLAGS += -fno-stack-protector

.c.o:
	$(CC) -MD -c $< -o $@ -std=gnu11 $(CFLAGS) $(CPPFLAGS)

.S.o:
	$(CC) -MD -c $< -o $@ $(CFLAGS) $(CPPFLAGS)

.cpp.o:
	$(CXX) -MD -c $< -o $@ -std=gnu++17 $(CXXFLAGS) $(CPPFLAGS)

clean:
	rm -f myos.kernel
	rm -f $(OBJS) *.o */*.o */*/*.o
	rm -f $(OBJS:.o=.d) *.d */*.d */*/*.d

install: install-headers install-kernel

install-headers:
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	cp -R --preserve=timestamps include/. $(DESTDIR)$(INCLUDEDIR)/.

install-kernel: myos.kernel
	mkdir -p $(DESTDIR)$(BOOTDIR)
	cp myos.kernel $(DESTDIR)$(BOOTDIR)

-include $(OBJS:.o=.d)
