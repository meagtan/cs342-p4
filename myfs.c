#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "myfs.h"

// Global Variables
char disk_name[128];   // name of virtual disk file
int  disk_size;        // size in bytes - a power of 2
int  disk_fd;          // disk file handle
int  disk_blockcount;  // block count on disk

/*
 * File System Implementation:
 * - Superblock contains above global variables, apart from disk_fd, also links to FAT, free blocks, etc.
 * - Directory structure implemented as sorted dynamic array of maximum size 128
 *   32K total blocks in disk, give 15 bits; represent block number by 16 bits
 *   128 * (32 + 2) < 2 blocks for directory entry
 * - FCBs contain file size (hence number of blocks) and list of blocks used, may reference FAT
 *   Kept in separate table for easy reference by FAT and open file table
 *   Each reference contains 6 bytes for FCB and 1 byte for valid/invalid, FCB table plus directory table have
 *   128 * (32 + 2 + 7) + 4 < 2 blocks
 * - FAT may also record allocated metadata blocks, in order to find free space to allocate FCBs etc.
 *   Otherwise, FAT only needs to record 3/4 of 32K, reducing its size to 12 blocks
 *
 * In memory:
 * - Cached copies of superblock, directory entries, FCBs (as necessary; allocated on heap), FAT
 * - Open file table, storing FCB index, starting block, current offset, current block
 *
 * At creation: initialize superblock, other tables and blocks may be all 0
 * At mount: load superblock, directory table into memory, initialize open file table
 * At unmount: write global variables and other structs into disk, close file etc.
 * Creating file: add entry to directory table, allocate FCB (possibly by finding free space from FAT)
 * Opening file: update open file table, record its FCB and starting block (as well as current offset etc.) in table, return table index
 *  For convenience, the current block (the block the current offset is in) may also be kept in the open file table
 * Reading from file: read byte by byte starting from offset, updating current block if necessary;
 *  stop if file end is reached or number of bytes read reaches n
 * Writing to file: same with read, but when file ends increment the size of the file, possibly allocating new block
 * Truncating file: move offset to new file size, deallocate subsequent blocks in order (first recording # of next block)
 * Seek to offset: change offset and also current block
 */

// TODO move to separate files

#include "dir.h"

struct superblock {
	char disk_name[128];
	int disk_size;
	int disk_blockcount;
	// TODO need not be kept
} superblock;

struct dir dir;

struct fat {
	BLOCKTYPE table[BLOCKCOUNT];
	// anything else?
} fat;

// may implement linked list structure here, each directory entry may link to first open file entry
// and open file entries may link to the next entry or -1
struct opentable {
	struct open_entry {
		int valid; // whether entry represents valid file or not; need indices not to change
		char filename[MAXFILENAMESIZE]; // search through dir
		BLOCKTYPE inum;  // index of fcb
		int offset;
		int size;        // keep up to date with fcb
		BLOCKTYPE start, // starting block
		          curr;  // current block
	} entries[MAXOPENFILES];
	int filenum; // no of open files
	int minfree; // smallest free index in table, -1 if full, may be updated after opening or closing files
} opentable;



/*
   Reads block blocknum into buffer buf.
   You will not modify the getblock() function.
   Returns -1 if error. Should not happen.
*/
int getblock (int blocknum, void *buf)
{
	int offset, n;

	if (blocknum >= disk_blockcount)
		return (-1); //error

	offset = lseek (disk_fd, blocknum * BLOCKSIZE, SEEK_SET);
	if (offset == -1) {
		printf ("lseek error\n");
		exit(0);

	}

	n = read (disk_fd, buf, BLOCKSIZE);
	if (n != BLOCKSIZE)
		return (-1);

	return (0);
}


/*
    Puts buffer buf into block blocknum.
    You will not modify the putblock() function
    Returns -1 if error. Should not happen.
*/
int putblock (int blocknum, void *buf)
{
	int offset, n;

	if (blocknum >= disk_blockcount)
		return (-1); //error

	offset = lseek (disk_fd, blocknum * BLOCKSIZE, SEEK_SET);
	if (offset == -1) {
		printf ("lseek error\n");
		exit (1);
	}

	n = write (disk_fd, buf, BLOCKSIZE);
	if (n != BLOCKSIZE)
		return (-1);

	return (0);
}


/*
   IMPLEMENT THE FUNCTIONS BELOW - You can implement additional
   internal functions.
 */

// TODO do I need to set global variables here? perhaps use putblock
int myfs_diskcreate (char *vdisk)
{
	int fd, i;
	char buf[BLOCKSIZE];

	// create new file with size DISKSIZE
	if (open(vdisk, O_RDWR | O_CREAT, 0666) == -1) {
		printf("disk create error %s\n", vdisk);
		exit(1);
	}

	// fill disk with zeros
	bzero((void *) buf, BLOCKSIZE);
	fd = open(vdisk, O_RDWR);
	for (i = 0; i < DISKSIZE / BLOCKSIZE; ++i) {
		if (write(fd, buf, BLOCKSIZE) != BLOCKSIZE) {
			close(fd);
			return -1;
		}
	}
	close(fd);

	return 0;
}


/* format disk of size dsize */
int myfs_makefs(char *vdisk)
{
	strcpy (disk_name, vdisk);
	disk_size = DISKSIZE;
	disk_blockcount = disk_size / BLOCKSIZE;

	disk_fd = open (disk_name, O_RDWR);
	if (disk_fd == -1) {
		printf ("disk open error %s\n", vdisk);
		exit(1);
	}

	// perform your format operations here.
	printf ("formatting disk=%s, size=%d\n", vdisk, disk_size);
	// TODO

	fsync (disk_fd);
	close (disk_fd);

	return (0);
}

/*
   Mount disk and its file system. This is not the same mount
   operation we use for real file systems:  in that the new filesystem
   is attached to a mount point in the default file system. Here we do
   not do that. We just prepare the file system in the disk to be used
   by the application. For example, we get FAT into memory, initialize
   an open file table, get superblock into into memory, etc.
*/

int myfs_mount (char *vdisk)
{
	struct stat finfo;

	strcpy (disk_name, vdisk);
	disk_fd = open (disk_name, O_RDWR);
	if (disk_fd == -1) {
		printf ("myfs_mount: disk open error %s\n", disk_name);
		exit(1);
	}

	fstat (disk_fd, &finfo);

	printf ("myfs_mount: mounting %s, size=%d\n", disk_name,
		(int) finfo.st_size);
	disk_size = (int) finfo.st_size;
	disk_blockcount = disk_size / BLOCKSIZE;

	// perform your mount operations here

	// allocate temporary buffer the size of 1 block, for better copying
	char *buf = malloc(BLOCKSIZE);

	// read superblock into buffer
	if (getblock(0, buf)) {
		printf("could not read superblock\n");
		return -1;
	}
	memcpy(&superblock, buf, sizeof(struct superblock)); // assuming sizeof superblock < BLOCKSIZE

	// copy elements of superblock into memory, or simply read global variables from buffer directly

	// read directory, assuming its size is a little over 1 block
	if (getblock(1, &dir) || getblock(2, buf)) {
		printf("could not read directory table\n");
		return -1;
	}
	memcpy(((char *) &dir) + BLOCKSIZE, buf, sizeof(struct dir) - BLOCKSIZE);

	// read FAT, assuming it is 16 blocks
	for (int i = 0; i < sizeof(struct fat) / BLOCKSIZE; ++i) {
		if (getblock(2 + i, ((char *) &fat) + i * BLOCKSIZE)) {
			printf("could not read FAT\n");
			return -1;
		}
	}

  	return (0);
}


int myfs_umount()
{
	// perform your unmount operations here

	// write your code

	fsync (disk_fd);
	close (disk_fd);
	return (0);
}


/* create a file with name filename */
int myfs_create(char *filename)
{
	// retrieve new FCB
	int inum = dir_add(dir, filename);
	if (inum == -1) // file already exists
		return -1;
	printf("created file %s with fcb index %d\n", filename, inum);
	return (0);
}


/* open file filename */
int myfs_open(char *filename)
{
	int index = -1;
	int inum = dir_get(dir, filename);

	// binary search through dir
	// if not found return index
	// else create new open file table entry
	// copy size and start from dir entry, curr = start, offset = 0
	if (inum == -1) {
		printf("file %s does not exist\n", filename);
		return -1;
	}

	// create new open file table entry for index
	index = open_add(open, filename, inum); // TODO add function
	if (index == -1) {
		printf("cannot open file %s\n", filename);
		return -1;
	}

	return (index);
}

/* close file filename */
int myfs_close(int fd)
{
	// check if open first
	// write cached blocks of file into disk, if any
	// remove from open file table
	// TODO write as member function for open

	return (0);
}

int myfs_delete(char *filename)
{
	struct inode inode;

	// retrieve file, checking if it actually exists
	// then remove it from directory entry and read its inode
	int inum = dir_remove(dir, filename, &inode);
	if (inum == -1) {
		printf("file %s does not exist\n", filename);
		return -1;
	}

	// TODO first check if open

	// deallocate data blocks in order
	BLOCKTYPE i = inode.start, j;
	while (fat.table[i] != 0) {
		j = fat.table[i];
		fat.table[i] = 0; // is more needed for deallocation?
		i = j;
	}

	// then remove directory entry etc.
	// already done in beginning

	// write to disk if blocks cached

	return (0);
}

int myfs_read(int fd, void *buf, int n)
{
	int bytes_read = -1;

	// check if file open
	// retrieve current block
	// read byte by byte until offset == size or bytes_read == n
	// if current block changes (size / BLOCKSIZE), retrieve new block and update curr

	return (bytes_read);
}

int myfs_write(int fd, void *buf, int n)
{
	int bytes_written = -1;

	// same as read, instead if offset == size and bytes_written < n,
	// increment size and if necessary allocate new block on fat

	return (bytes_written);
}

int myfs_truncate(int fd, int size)
{
	// compare size with current size
	// then seek size in fat
	// deallocate every block after current block in order on fat
	// on current block, just change file size to size
	return (0);
}


int myfs_seek(int fd, int offset)
{
	int position = -1;

	// traverse fat
	// compare offset with size

	return (position);
}

int myfs_filesize (int fd)
{
	int size = -1;

	// retrieve open table entry

	return (size);
}


void myfs_print_dir ()
{
	// linear scan through dir
}


void myfs_print_blocks (char *  filename)
{
	// find filename on dir
	// for each file, traverse fat from their start
}


