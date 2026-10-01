menu "File Systems"

config FS_VFS
    bool "Virtual File System (VFS)"
    default y

if FS_VFS

menu "BlockOS file systems"

config FS_EXT2
    bool "EXT2"
    default y

config FS_EXT3
    bool "EXT3"
    default y

config FS_EXT4
    bool "EXT4"
    default y

config FS_FAT32
    bool "FAT32"
    default y

config FS_EXFAT
    bool "exFAT"
    default y

config FS_NTFS
    bool "NTFS"
    default y

config FS_BTRFS
    bool "Btrfs"
    default y

config FS_JFS
    bool "JFS"
    default y

config FS_ISO9660
    bool "ISO9660"
    default y

config FS_UDF
    bool "UDF"
    default y

config FS_UFS
    bool "UFS"
    default y

config FS_UFS2
    bool "UFS2"
    default y

config FS_RAMFS
    bool "ramfs"
    default y

config FS_TMPFS
    bool "tmpfs"
    default n

config FS_SQUASHFS
    bool "SquashFS"
    default n

config FS_EROFS
    bool "EROFS"
    default n

config FS_LUA_ELF
    bool "Lua ELF filesystem integration"
    default n

endmenu

menu "File system features"

config FS_PROC
    bool "procfs"
    default y

config FS_SYSFS
    bool "sysfs"
    default y

config FS_MOUNT
    bool "Mount support"
    default y

config FS_UNMOUNT
    bool "Unmount support"
    default y

config FS_PERMISSIONS
    bool "File permissions"
    default y

endmenu

endif

endmenu
