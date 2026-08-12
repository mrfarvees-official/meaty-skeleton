#!/bin/sh
set -e

. ./iso.sh

DISK=disk.img

if [ ! -f "$DISK" ]; then
    echo "Creating $DISK..."
    qemu-img create -f raw "$DISK" 64M

    echo "Creating GPT partition table..."

    printf 'label: gpt\nstart=2048,type=linux\n' | \
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
    -drive file="$DISK",format=raw,if=ide,index=0,media=disk \
    -drive file=myos.iso,format=raw,if=ide,index=2,media=cdrom,readonly=on