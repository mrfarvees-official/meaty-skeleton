#!/bin/sh
set -e

. ./iso.sh

DISK=disk.img

if [ ! -f "$DISK" ]; then
    echo "Creating $DISK..."
    qemu-img create -f raw "$DISK" 64M

    echo "Creating partition table..."

    printf 'label: dos\nstart=2048,type=83,bootable\n' | \
        sfdisk "$DISK"

    echo "Creating ext2 filesystem..."

    LOOP=$(sudo losetup --find --show --partscan "$DISK")

    sudo mkfs.ext2 "${LOOP}p1"

    sudo losetup -d "$LOOP"

    echo "Disk initialized."
fi

qemu-system-$(./target-triplet-to-arch.sh "$HOST") \
    -m 128M \
    -smp 2 \
    -boot order=d \
    -cdrom myos.iso \
    -drive file="$DISK",format=raw,if=ide,index=0,media=disk