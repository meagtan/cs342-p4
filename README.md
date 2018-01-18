# cs342fall2017_p4
CS342 Fall 2017 project 4

Ata Deniz Aydın
21502637

Auxiliary source code relating to in-memory structures have been implemented in dir.* and opentable.*, and included in myfs.c and the makefile. Details about the implementation of the file system, as well as statistics collected, may be found in report.pdf.

In order to use the library for multiple processes, link application files with -lrt (as in the makefile) and uncomment "#include <sys/mman.h>" in myfs.c.
