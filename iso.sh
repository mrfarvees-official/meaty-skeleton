#!/bin/sh
set -e
. ./build.sh

mkdir -p isodir
mkdir -p isodir/boot
mkdir -p isodir/boot/grub

cp $SYSROOT/boot/myos.kernel isodir/boot/myos.kernel
cat > isodir/boot/grub/grub.cfg << EOF
set timeout=15
set default=0

menuentry "myos" {
	multiboot /boot/myos.kernel
	boot
}
EOF
grub-mkrescue -o myos.iso isodir
