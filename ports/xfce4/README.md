# Xfce runtime support for BlockOS

This directory intentionally does **not** contain the full Xfce build system.
The target BlockOS installation is expected to receive prebuilt runtime packages
through `blkpkg`.

The BlockOS-side work in this tree provides the libc/kernel ABI used by the
runtime stack: dynamic ELF startup, mmap/munmap/mprotect/brk, pthread/futex,
AF_UNIX sockets, pipe, poll/epoll, stat/access, file creation/truncation/write,
stdio, time, select, uname, basic process spawning (`fork` -> `execve`), and
common POSIX compatibility entry points.

Still intentionally outside this first runtime pass:
- the full upstream Xfce source tree;
- compiler/Meson/Ninja/pkg-config tooling on the installed OS;
- a general-purpose `dlopen()` loader API;
- generic filesystem symlink creation/resolution across every VFS backend;
- persistence semantics beyond the current VFS implementation.

Core Xfce can therefore be packaged independently from the BlockOS ISO, but a
successful real Xfce desktop boot still requires the matching prebuilt X11,
GTK/GLib and Xfce packages plus those remaining runtime items where a component
actually uses them.
