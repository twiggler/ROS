# ROS

Roel Operating System. 

- x64 Long mode.

## Dependencies

- BOOTBOOT bootloader

## Instructions

Debug using QEMU:

qemu-system-x86_64 -s -S -bios /usr/share/qemu/OVMF.fd -drive format=raw,file=fat:rw:initrd/build