#!/bin/bash
set -e

# Start ROS in qemu
make
tmp_dir=$(mktemp -d -t)
echo Creating boot disk in $tmp_dir
mkdir -p $tmp_dir/EFI/BOOT
mkdir $tmp_dir/BOOTBOOT

cp bootboot/dist/bootboot.efi $tmp_dir/EFI/BOOT/BOOTX64.EFI
cp build/kernel.x86_64.elf $tmp_dir/BOOTBOOT/INITRD

qemu-system-x86_64 -s -S -bios /usr/share/qemu/OVMF.fd -drive format=raw,file=fat:rw:$tmp_dir
