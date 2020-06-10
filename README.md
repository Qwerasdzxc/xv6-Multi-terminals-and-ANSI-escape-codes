# Multiple terminals and ANSI escape codes

The goal of the project was to modify the xv6 system to support multi-terminal operation at the same time, with support for changing the color of the screen using ANSI escape codes.
Following is the added functionalities:

* Change the currently active terminal
* Read and write at all terminals
* Support for ANSI escape codes for colors on terminals

xv6 is a re-implementation of Dennis Ritchie's and Ken Thompson's Unix
Version 6 (v6).  xv6 loosely follows the structure and style of v6,
but is implemented for a modern x86-based multiprocessor using ANSI C.

BUILDING AND RUNNING XV6

To build xv6 on an x86 ELF machine (like Linux or FreeBSD), run
"make". On non-x86 or non-ELF machines (like OS X, even on x86), you
will need to install a cross-compiler gcc suite capable of producing
x86 ELF binaries (see https://pdos.csail.mit.edu/6.828/).
Then run "make TOOLPREFIX=i386-jos-elf-". Now install the QEMU PC
simulator and run "make qemu".
