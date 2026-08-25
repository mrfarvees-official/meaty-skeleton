#!/bin/sh
set -e

. ./iso.sh


DISK=disk.img

RECREATE_BENCH_DISK=${RECREATE_BENCH_DISK:-true}

#
# Existing optional, non-destructive system font installation.
#
INSTALL_FONT_ASSET=${INSTALL_FONT_ASSET:-true}

#
# Optional, non-destructive GUI image asset installation.
#
# This updates only:
#
#     /wallpapers
#     /icons
#
# on the existing ext2 filesystem.
#
INSTALL_GUI_ASSETS=${INSTALL_GUI_ASSETS:-true}


#
# ------------------------------------------------------------
# Local asset sources
# ------------------------------------------------------------
#

CONSOLAS_TTF=${MEATY_CONSOLAS_TTF:-assets/local/Consolas.ttf}

GUI_WALLPAPER=${MEATY_WALLPAPER:-assets/wallpapers/default.png}

GUI_ICON_ROOT=${MEATY_ICON_ROOT:-assets/icons}


DISK_SIZE=64M
BENCH_FILE_SIZE=1M
BENCH_FILE=double.txt


#
# ------------------------------------------------------------
# Font installation
# ------------------------------------------------------------
#

copy_font_to_mounted_root()
{
    ROOT="$1"

    if [ ! -f "$CONSOLAS_TTF" ]; then
        echo "Error: Consolas TTF not found:"
        echo "  $CONSOLAS_TTF"
        echo
        echo "Set MEATY_CONSOLAS_TTF=/path/to/Consolas.ttf"
        exit 1
    fi

    echo "Installing system font as /fonts/Consolas.ttf..."

    sudo mkdir -p \
        "$ROOT/fonts"

    sudo cp \
        "$CONSOLAS_TTF" \
        "$ROOT/fonts/Consolas.ttf"

    sync

    echo "Installed font:"

    sudo ls -lh \
        "$ROOT/fonts/Consolas.ttf"
}


#
# ------------------------------------------------------------
# GUI asset installation
# ------------------------------------------------------------
#

validate_gui_assets()
{
    if [ ! -f "$GUI_WALLPAPER" ]; then
        echo "Error: GUI wallpaper not found:"
        echo "  $GUI_WALLPAPER"
        exit 1
    fi

    if [ ! -d "$GUI_ICON_ROOT/apps" ]; then
        echo "Error: app icon directory not found:"
        echo "  $GUI_ICON_ROOT/apps"
        exit 1
    fi

    if [ ! -d "$GUI_ICON_ROOT/system" ]; then
        echo "Error: system icon directory not found:"
        echo "  $GUI_ICON_ROOT/system"
        exit 1
    fi

    set -- "$GUI_ICON_ROOT"/apps/*.png

    if [ ! -e "$1" ]; then
        echo "Error: no app PNG icons found:"
        echo "  $GUI_ICON_ROOT/apps"
        exit 1
    fi

    set -- "$GUI_ICON_ROOT"/system/*.png

    if [ ! -e "$1" ]; then
        echo "Error: no system PNG icons found:"
        echo "  $GUI_ICON_ROOT/system"
        exit 1
    fi
}


copy_gui_assets_to_mounted_root()
{
    ROOT="$1"

    validate_gui_assets

    echo "Installing GUI image assets..."

    sudo mkdir -p \
        "$ROOT/wallpapers" \
        "$ROOT/icons/apps" \
        "$ROOT/icons/system"

    sudo cp \
        "$GUI_WALLPAPER" \
        "$ROOT/wallpapers/default.png"

    for ICON in "$GUI_ICON_ROOT"/apps/*.png
    do
        echo "Installing app icon: $(basename "$ICON")"

        sudo cp \
            "$ICON" \
            "$ROOT/icons/apps/"
    done

    for ICON in "$GUI_ICON_ROOT"/system/*.png
    do
        echo "Installing system icon: $(basename "$ICON")"

        sudo cp \
            "$ICON" \
            "$ROOT/icons/system/"
    done

    sync

    echo "Installed wallpaper:"

    sudo ls -lh \
        "$ROOT/wallpapers/default.png"

    echo "Installed app icons:"

    sudo ls -lh \
        "$ROOT/icons/apps/"*.png

    echo "Installed system icons:"

    sudo ls -lh \
        "$ROOT/icons/system/"*.png
}


#
# ------------------------------------------------------------
# Fresh persistent disk creation
# ------------------------------------------------------------
#

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

    #
    # A newly created filesystem receives all required GUI assets.
    #
    copy_font_to_mounted_root \
        /mnt/meaty-bench

    copy_gui_assets_to_mounted_root \
        /mnt/meaty-bench

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

    sudo rmdir \
        /mnt/meaty-bench \
        2>/dev/null || true

    trap - EXIT INT TERM

    echo "Benchmark disk ready."
}


#
# ------------------------------------------------------------
# Existing disk: font-only installation
# ------------------------------------------------------------
#

install_font_into_existing_disk()
{
    if [ ! -f "$DISK" ]; then
        echo "Error: $DISK does not exist."
        exit 1
    fi

    LOOP=""

    cleanup_font_install()
    {
        if [ -n "$LOOP" ]; then
            sudo umount "${LOOP}p1" 2>/dev/null || true
            sudo losetup -d "$LOOP" 2>/dev/null || true
        fi

        sudo rmdir /mnt/meaty-font-install 2>/dev/null || true
    }

    trap cleanup_font_install EXIT INT TERM

    echo "Attaching existing persistent disk..."

    LOOP=$(sudo losetup \
        --find \
        --show \
        --partscan \
        "$DISK")

    sudo mkdir -p \
        /mnt/meaty-font-install

    echo "Mounting existing ext2 filesystem..."

    sudo mount \
        "${LOOP}p1" \
        /mnt/meaty-font-install

    copy_font_to_mounted_root \
        /mnt/meaty-font-install

    echo "Unmounting existing filesystem..."

    sudo umount \
        /mnt/meaty-font-install

    sudo losetup -d \
        "$LOOP"

    LOOP=""

    sudo rmdir \
        /mnt/meaty-font-install \
        2>/dev/null || true

    trap - EXIT INT TERM

    echo "Font installation complete."
}


#
# ------------------------------------------------------------
# Existing disk: GUI image assets
# ------------------------------------------------------------
#

install_gui_assets_into_existing_disk()
{
    if [ ! -f "$DISK" ]; then
        echo "Error: $DISK does not exist."
        exit 1
    fi

    validate_gui_assets

    LOOP=""

    cleanup_gui_asset_install()
    {
        if [ -n "$LOOP" ]; then
            sudo umount "${LOOP}p1" 2>/dev/null || true
            sudo losetup -d "$LOOP" 2>/dev/null || true
        fi

        sudo rmdir /mnt/meaty-gui-assets 2>/dev/null || true
    }

    trap cleanup_gui_asset_install EXIT INT TERM

    echo "Attaching existing persistent disk..."

    LOOP=$(sudo losetup \
        --find \
        --show \
        --partscan \
        "$DISK")

    sudo mkdir -p \
        /mnt/meaty-gui-assets

    echo "Mounting existing ext2 filesystem..."

    sudo mount \
        "${LOOP}p1" \
        /mnt/meaty-gui-assets

    copy_gui_assets_to_mounted_root \
        /mnt/meaty-gui-assets

    echo "Unmounting existing filesystem..."

    sudo umount \
        /mnt/meaty-gui-assets

    sudo losetup -d \
        "$LOOP"

    LOOP=""

    sudo rmdir \
        /mnt/meaty-gui-assets \
        2>/dev/null || true

    trap - EXIT INT TERM

    echo "GUI asset installation complete."
}


#
# ------------------------------------------------------------
# Disk policy
# ------------------------------------------------------------
#

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


#
# ------------------------------------------------------------
# Optional non-destructive font update
# ------------------------------------------------------------
#

case "$INSTALL_FONT_ASSET" in
    true)
        if [ "$RECREATE_BENCH_DISK" = false ]; then
            install_font_into_existing_disk
        fi
        ;;

    false)
        ;;

    *)
        echo "Error: INSTALL_FONT_ASSET must be true or false."
        exit 1
        ;;
esac


#
# ------------------------------------------------------------
# Optional non-destructive GUI asset update
# ------------------------------------------------------------
#

case "$INSTALL_GUI_ASSETS" in
    true)
        if [ "$RECREATE_BENCH_DISK" = false ]; then
            install_gui_assets_into_existing_disk
        fi
        ;;

    false)
        ;;

    *)
        echo "Error: INSTALL_GUI_ASSETS must be true or false."
        exit 1
        ;;
esac


echo "Starting QEMU..."

qemu-system-$(./target-triplet-to-arch.sh "$HOST") \
    -m 128M \
    -smp 2 \
    -boot order=d \
    -vga std \
    -display gtk \
    -debugcon stdio \
    -device ich9-ahci,id=ahci \
    -drive id=sata_disk,file="$DISK",format=raw,if=none \
    -device ide-hd,drive=sata_disk,bus=ahci.0 \
    -drive file=myos.iso,format=raw,if=ide,index=2,media=cdrom,readonly=on