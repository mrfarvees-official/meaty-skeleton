#!/bin/sh
set -e

. ./iso.sh

DISK=disk.img

#
# Benchmark disk setup.
#
# true:
#   recreate disk.img
#   create ext2 filesystem
#   create benchmark file
#   install all userspace .nex executables into /bin
#
# false:
#   leave disk.img untouched and just boot QEMU
#
RECREATE_BENCH_DISK=false

#
# Disk and benchmark sizes.
#
DISK_SIZE=256M
BENCH_FILE_SIZE=32M
BENCH_FILE=double.txt

create_benchmark_disk()
{
    echo "Recreating $DISK..."

    rm -f "$DISK"

    qemu-img create \
        -f raw \
        "$DISK" \
        "$DISK_SIZE"

    echo "Creating GPT partition table..."

    printf 'label: gpt\nstart=2048,type=linux\n' | \
        sfdisk "$DISK"

    LOOP=""

    cleanup()
    {
        if [ -n "$LOOP" ]; then
            sudo umount "${LOOP}p1" 2>/dev/null || true
            sudo losetup -d "$LOOP" 2>/dev/null || true
        fi

        sudo rmdir /mnt/meaty-bench 2>/dev/null || true
    }

    trap cleanup EXIT INT TERM

    echo "Attaching disk..."

    LOOP=$(sudo losetup \
        --find \
        --show \
        --partscan \
        "$DISK")

    echo "Creating ext2 filesystem..."

    sudo mkfs.ext2 \
        -F \
        "${LOOP}p1"

    echo "Creating benchmark file: $BENCH_FILE_SIZE..."

    rm -f "$BENCH_FILE"

    truncate \
        -s "$BENCH_FILE_SIZE" \
        "$BENCH_FILE"

    echo "Mounting filesystem..."

    sudo mkdir -p /mnt/meaty-bench

    sudo mount \
        "${LOOP}p1" \
        /mnt/meaty-bench

    echo "Copying benchmark file as /double.txt..."

    sudo cp \
        "$BENCH_FILE" \
        /mnt/meaty-bench/double.txt

    echo "Creating /grow.txt..."

    sudo dd \
        if=/dev/zero \
        of=/mnt/meaty-bench/grow.txt \
        bs=4096 \
        count=1 \
        conv=fsync

    echo "Installing userspace executables..."

    sudo mkdir -p \
        /mnt/meaty-bench/bin

    set -- "$SYSROOT"/bin/*.nex

    if [ ! -e "$1" ]; then
        echo "Error: no userspace .nex executables found in $SYSROOT/bin."
        exit 1
    fi

    for PROGRAM in "$@"
    do
        echo "Installing $(basename "$PROGRAM")..."

        sudo cp \
            "$PROGRAM" \
            /mnt/meaty-bench/bin/
    done

    sync

    echo "Installed userspace executables:"

    sudo ls -lh \
        /mnt/meaty-bench/bin/*.nex

    echo "Benchmark file:"

    ls -lh "$BENCH_FILE"

    sudo ls -lh \
        /mnt/meaty-bench/double.txt

    echo "Unmounting filesystem..."

    sudo umount /mnt/meaty-bench

    sudo losetup -d "$LOOP"

    LOOP=""

    sudo rmdir /mnt/meaty-bench 2>/dev/null || true

    trap - EXIT INT TERM

    echo "Benchmark disk ready."
}

case "$RECREATE_BENCH_DISK" in
    true)
        create_benchmark_disk
        ;;

    false)
        if [ ! -f "$DISK" ]; then
            echo "Error: $DISK does not exist."
            echo "Set RECREATE_BENCH_DISK=true first."
            exit 1
        fi
        ;;

    *)
        echo "Error: RECREATE_BENCH_DISK must be true or false."
        exit 1
        ;;
esac

echo "Starting QEMU..."

qemu-system-$(./target-triplet-to-arch.sh "$HOST") \
    -m 128M \
    -smp 2 \
    -boot order=d \
    -device ich9-ahci,id=ahci \
    -drive id=sata_disk,file="$DISK",format=raw,if=none \
    -device ide-hd,drive=sata_disk,bus=ahci.0 \
    -drive file=myos.iso,format=raw,if=ide,index=2,media=cdrom,readonly=on