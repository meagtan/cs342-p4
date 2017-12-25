#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "myfs.h"

#define MEASURE(stmt) do {			\
	start = clock();			\
	assert(stmt);				\
	end = clock();				\
	diff += end - start;			\
} while (0)

int main(int argc, char *argv[])
{
	char diskname[128];
	char filename[16][MAXFILENAMESIZE];
	int i, j, k;
	// int fd0, fd1, fd2;       // file handles
	char buf[MAXREADWRITE];
	clock_t start, end, diff;

	int fd[16];
	/*
	strcpy (filename[0], "file0");
	strcpy (filename[1], "file1");
	strcpy (filename[2], "file2");
	*/

	for (i = 0; i < 16; ++i)
		sprintf(filename[i], "file%d", i);

	if (argc != 2) {
		printf ("usage: app <diskname>\n");
		exit (1);
	}

	strcpy (diskname, argv[1]);

	// test mounting, creating, writing, reading for different files and sizes
	int siz; // size of writes and reads
	for (i = 0, siz = 100; i < 5; ++i, siz *= 10) {
		diff = 0;
		MEASURE(!myfs_mount(diskname));
		fprintf(stderr, "mount\t%d\t%ld\n", 16 * (i ? siz / 10 : 0), diff);

		// create or open each file
		for (j = 0; j < 16; ++j) {
			diff = 0;
			MEASURE((fd[j] = myfs_create(filename[j])) != -1);
			fprintf(stderr, "%s\t%d\t%d\t%ld\n", i ? "open" : "create", j, myfs_filesize(fd[j]), diff);
		}

		// write siz bytes to each file
		for (j = 0; j < 16; ++j) {
			// fprintf(stderr, "seek\t%d\t%d\n", j, myfs_seek(fd[j], siz));
			diff = 0;
			for (k = 0; k < siz / MAXREADWRITE; ++k) {
				MEASURE(myfs_write(fd[j], buf, MAXREADWRITE) == MAXREADWRITE);
			}
			MEASURE(myfs_write(fd[j], buf, siz % MAXREADWRITE) == siz % MAXREADWRITE);
			fprintf(stderr, "write\t%d\t%d\t%ld\n", j, myfs_filesize(fd[j]), diff);
		}

		// close each file
		for (j = 0; j < 16; ++j) {
			k = myfs_filesize(fd[j]);
			diff = 0;
			MEASURE(myfs_close(fd[j]) == 0);
			fprintf(stderr, "close\t%d\t%d\t%ld\n", j, k, diff);
		}

		// open each file
		for (j = 0; j < 16; ++j) {
			diff = 0;
			MEASURE((fd[j] = myfs_open(filename[j])) != -1);
			fprintf(stderr, "open\t%d\t%d\t%ld\n", j, myfs_filesize(fd[j]), diff);
		}

		// read siz bytes from each file
		for (j = 0; j < 16; ++j) {
			diff = 0;
			for (k = 0; k < siz / MAXREADWRITE; ++k) {
				MEASURE(myfs_read(fd[j], buf, MAXREADWRITE) == MAXREADWRITE);
			}
			MEASURE(myfs_read(fd[j], buf, siz % MAXREADWRITE) == siz % MAXREADWRITE);
			fprintf(stderr, "read\t%d\t%d\t%ld\n", j, myfs_filesize(fd[j]), diff);
			// myfs_truncate(fd[j], siz);
			// fprintf(stderr, "truncate\t%d\t%d\n", j, myfs_filesize(fd[j]));
		}

		// close each file
		for (j = 0; j < 16; ++j) {
			k = myfs_filesize(fd[j]);
			diff = 0;
			MEASURE(myfs_close(fd[j]) == 0);
			fprintf(stderr, "close\t%d\t%d\t%ld\n", j, k, diff);
		}

		// if at last iteration delete each file
		if (i == 4) {
			for (j = 0; j < 16; ++j) {
				diff = 0;
				MEASURE(myfs_delete(filename[j]) == 0);
				fprintf(stderr, "delete\t%d\t%ld\n", j, diff);
			}
		}

		// unmount disk
		diff = 0;
		MEASURE(myfs_umount() == 0);
		fprintf(stderr, "unmount\t%d\t%ld\n", 16 * siz, diff);
	}

}

/*
	if (myfs_mount (diskname) != 0) {
		printf ("could not mound %s\n", diskname); 
		exit (1); 
	}
	else 
		printf ("filesystem %s mounted\n", diskname); 

	for (i=0; i<3; ++i) {
		if (myfs_create (filename[i]) != 0) {
			printf ("could not create file %s\n", filename[i]); 
			exit (1); 
		}
		else 
			printf ("file %s created\n", filename[i]); 
	}

	for (int j = 0; j < 2; ++j) {

	fd0 = myfs_open (filename[j]);
	if (fd0 == -1) {
		printf ("file open failed: %s\n", filename[j]);
		exit (1);
	}

	for (i=0; i<10; ++i) {
		n = myfs_write (fd0, buf, 500);
		if (n != 500) {
			printf ("vsfs_write failed\n");
			exit (1);
		}
	}

	myfs_close (fd0);

	fd0 = myfs_open (filename[j]);

	for (i=0; i<(10*500/100); ++i)
	{
		n = myfs_read (fd0, buf, 100);
		if (n != 100) {
			printf ("vsfs_read failed\n");
			exit(1);
		}
	}

	myfs_close (fd0);

	}

	fd1 = myfs_open (filename[1]);
	fd2 = myfs_open (filename[2]);

	myfs_close (fd1);
	myfs_close (fd2);

	myfs_print_dir();
	myfs_print_blocks(filename[0]);
	myfs_print_blocks(filename[1]);

	myfs_umount();

	return (0);
}
*/
