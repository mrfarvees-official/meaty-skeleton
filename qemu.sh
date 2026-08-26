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
INSTALL_GUI_ASSETS=${INSTALL_GUI_ASSETS:-true}

SHELL_ASSET_ROOT=${MEATY_SHELL_ASSET_ROOT:-assets/shell}
INSTALL_SHELL_ASSETS=${INSTALL_SHELL_ASSETS:-true}


#
# ------------------------------------------------------------
# Local asset sources
# ------------------------------------------------------------
#

FONT_ROOT=${MEATY_FONT_ROOT:-assets/local}

GUI_WALLPAPER_ROOT=${MEATY_WALLPAPER_ROOT:-assets/wallpapers}

GUI_ICON_ROOT=${MEATY_ICON_ROOT:-assets/icons}


DISK_SIZE=64M
BENCH_FILE_SIZE=1M
BENCH_FILE=double.txt


#
# ------------------------------------------------------------
# Font installation
# ------------------------------------------------------------
#

validate_font_assets()
{
    if [ ! -d "$FONT_ROOT" ]; then
        echo "Error: font directory not found:"
        echo "  $FONT_ROOT"
        exit 1
    fi

    set -- "$FONT_ROOT"/*.ttf

    if [ ! -e "$1" ]; then
        echo "Error: no TTF font files found:"
        echo "  $FONT_ROOT"
        exit 1
    fi
}


copy_fonts_to_mounted_root()
{
    ROOT="$1"

    validate_font_assets

    echo "Installing system fonts..."

    sudo mkdir -p \
        "$ROOT/fonts"

    for FONT in "$FONT_ROOT"/*.ttf
    do
        echo "Installing font: $(basename "$FONT")"

        sudo cp \
            "$FONT" \
            "$ROOT/fonts/"
    done

    sync

    echo "Installed fonts:"

    sudo ls -lh \
        "$ROOT/fonts/"*.ttf
}


#
# ------------------------------------------------------------
# GUI asset installation
# ------------------------------------------------------------
#

validate_gui_assets()
{
    if [ ! -d "$GUI_WALLPAPER_ROOT" ]; then
        echo "Error: wallpaper directory not found:"
        echo "  $GUI_WALLPAPER_ROOT"
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

    set -- "$GUI_WALLPAPER_ROOT"/*

    if [ ! -e "$1" ]; then
        echo "Error: no wallpaper files found:"
        echo "  $GUI_WALLPAPER_ROOT"
        exit 1
    fi

    set -- "$GUI_ICON_ROOT"/apps/*

    if [ ! -e "$1" ]; then
        echo "Error: no app icon files found:"
        echo "  $GUI_ICON_ROOT/apps"
        exit 1
    fi

    set -- "$GUI_ICON_ROOT"/system/*

    if [ ! -e "$1" ]; then
        echo "Error: no system icon files found:"
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

    #
    # Copy all wallpaper files.
    #
    for WALLPAPER in "$GUI_WALLPAPER_ROOT"/*
    do
        if [ -f "$WALLPAPER" ]; then
            echo "Installing wallpaper: $(basename "$WALLPAPER")"

            sudo cp \
                "$WALLPAPER" \
                "$ROOT/wallpapers/"
        fi
    done

    #
    # Copy all application icon files.
    #
    for ICON in "$GUI_ICON_ROOT"/apps/*
    do
        if [ -f "$ICON" ]; then
            echo "Installing app icon: $(basename "$ICON")"

            sudo cp \
                "$ICON" \
                "$ROOT/icons/apps/"
        fi
    done

    #
    # Copy all system icon files.
    #
    for ICON in "$GUI_ICON_ROOT"/system/*
    do
        if [ -f "$ICON" ]; then
            echo "Installing system icon: $(basename "$ICON")"

            sudo cp \
                "$ICON" \
                "$ROOT/icons/system/"
        fi
    done

    sync

    echo "Installed wallpapers:"

    sudo ls -lh \
        "$ROOT/wallpapers/"

    echo "Installed app icons:"

    sudo ls -lh \
        "$ROOT/icons/apps/"

    echo "Installed system icons:"

    sudo ls -lh \
        "$ROOT/icons/system/"
}

copy_shell_assets_to_mounted_root()
{
    ROOT="$1"

    if [ ! -d "$SHELL_ASSET_ROOT/apps" ]; then
        echo "Error: shell app descriptor directory not found:"
        echo "  $SHELL_ASSET_ROOT/apps"
        exit 1
    fi

    if [ ! -d "$SHELL_ASSET_ROOT/taskbar" ]; then
        echo "Error: shell taskbar directory not found:"
        echo "  $SHELL_ASSET_ROOT/taskbar"
        exit 1
    fi

    echo "Installing shell metadata..."

    sudo mkdir -p \
        "$ROOT/apps" \
        "$ROOT/taskbar"

    for APP in "$SHELL_ASSET_ROOT"/apps/*.app
    do
        if [ -f "$APP" ]; then
            echo "Installing application descriptor: $(basename "$APP")"

            sudo cp \
                "$APP" \
                "$ROOT/apps/"
        fi
    done

    for LINK in "$SHELL_ASSET_ROOT"/taskbar/*.link
    do
        if [ -f "$LINK" ]; then
            echo "Installing taskbar link: $(basename "$LINK")"

            sudo cp \
                "$LINK" \
                "$ROOT/taskbar/"
        fi
    done

    sync

    echo "Installed application descriptors:"
    sudo ls -lh "$ROOT/apps/"

    echo "Installed taskbar links:"
    sudo ls -lh "$ROOT/taskbar/"
}

install_shell_assets_into_existing_disk()
{
    if [ ! -f "$DISK" ]; then
        echo "Error: $DISK does not exist."
        exit 1
    fi

    LOOP=""

    cleanup_shell_asset_install()
    {
        if [ -n "$LOOP" ]; then
            sudo umount "${LOOP}p1" 2>/dev/null || true
            sudo losetup -d "$LOOP" 2>/dev/null || true
        fi

        sudo rmdir /mnt/meaty-shell-assets 2>/dev/null || true
    }

    trap cleanup_shell_asset_install EXIT INT TERM

    echo "Attaching existing persistent disk..."

    LOOP=$(sudo losetup \
        --find \
        --show \
        --partscan \
        "$DISK")

    sudo mkdir -p \
        /mnt/meaty-shell-assets

    echo "Mounting existing ext2 filesystem..."

    sudo mount \
        "${LOOP}p1" \
        /mnt/meaty-shell-assets

    copy_shell_assets_to_mounted_root \
        /mnt/meaty-shell-assets

    echo "Unmounting existing filesystem..."

    sudo umount \
        /mnt/meaty-shell-assets

    sudo losetup -d \
        "$LOOP"

    LOOP=""

    sudo rmdir \
        /mnt/meaty-shell-assets \
        2>/dev/null || true

    trap - EXIT INT TERM

    echo "Shell metadata installation complete."
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
    # Newly created filesystem receives all fonts.
    #
    copy_fonts_to_mounted_root \
        /mnt/meaty-bench

    #
    # Newly created filesystem receives all GUI assets.
    #
    copy_gui_assets_to_mounted_root \
        /mnt/meaty-bench

    copy_shell_assets_to_mounted_root \
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
# Existing disk: font installation
# ------------------------------------------------------------
#

install_fonts_into_existing_disk()
{
    if [ ! -f "$DISK" ]; then
        echo "Error: $DISK does not exist."
        exit 1
    fi

    validate_font_assets

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

    copy_fonts_to_mounted_root \
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
            install_fonts_into_existing_disk
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

case "$INSTALL_SHELL_ASSETS" in
    true)
        if [ "$RECREATE_BENCH_DISK" = false ]; then
            install_shell_assets_into_existing_disk
        fi
        ;;

    false)
        ;;

    *)
        echo "Error: INSTALL_SHELL_ASSETS must be true or false."
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