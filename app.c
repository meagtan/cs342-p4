#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "myfs.h"

int main(int argc, char *argv[])
{
	char diskname[128]; 
	char filename[16][MAXFILENAMESIZE]; 
	int i, n; 
	int fd0, fd1, fd2;       // file handles
	char buf[MAXREADWRITE]; 

	strcpy (filename[0], "file0"); 
	strcpy (filename[1], "file1"); 
	strcpy (filename[2], "file2"); 
	
	if (argc != 2) {
		printf ("usage: app <diskname>\n"); 
		exit (1);
	}
       
	strcpy (diskname, argv[1]); 
	
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
