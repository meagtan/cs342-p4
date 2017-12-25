#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/mman.h>

#include "myfs.h"

// Global Variables
char disk_name[128];   // name of virtual disk file
int  disk_size;        // size in bytes - a power of 2
int  disk_fd = 0;      // disk file handle
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
 *   Otherwise, FAT only needs to record 3/4 of 32K block numbers, reducing its size to 12 blocks
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


#include "dir.h"
#include "opentable.h"

// directory entry, inode table, FAT etc. locations hardcoded, need not be kept here
struct superblock {
	char disk_name[128];
	int disk_size;
	int disk_blockcount;
	// TODO need not be kept
} superblock;

// shared memory
// TODO using shm requires linking to another static library
int shm_fd;
size_t shm_size = 2*BLOCKSIZE + sizeof(struct opentable); // 2 blocks for dir, to make it align better
char shm_name[133]; // myfs_diskname

// make these point to shared memory
struct dir *dir;
struct opentable *opentable;

BLOCKTYPE fat_getnext(BLOCKTYPE blk);
BLOCKTYPE fat_setnext(BLOCKTYPE blk); // finds and sets next block for blk (0 represents new file), if none available returns 0
int fat_dealloc(BLOCKTYPE blk); // deallocates block

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

int myfs_diskcreate (char *vdisk)
{
	int i;
	char buf[BLOCKSIZE];

	// create new file with size DISKSIZE
	if (open(vdisk, O_RDWR | O_CREAT, 0666) == -1) {
		// printf("disk create error %s\n", vdisk);
		exit(1);
	}

	// fill disk with zeros (not actually necessary for formatting)
	bzero(buf, BLOCKSIZE);

	// set for putblock
	disk_fd = open(vdisk, O_RDWR);
	disk_size = DISKSIZE;
	disk_blockcount = disk_size / BLOCKSIZE;

	for (i = 0; i < disk_blockcount; ++i) {
		if (putblock(i, buf)) {
			close(disk_fd);
			return -1;
		}
	}

	close(disk_fd);
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
		// printf ("disk open error %s\n", vdisk);
		exit(1);
	}

	// perform your format operations here.
	// printf ("formatting disk=%s, size=%d\n", vdisk, disk_size);

	char buf[BLOCKSIZE];
	int i;
	memset(buf, 0, BLOCKSIZE);

	for (i = 0; i < disk_blockcount / 4; ++i)
		if (putblock(i, buf))
			break;

	// write superblock
	strcpy(superblock.disk_name, disk_name);
	superblock.disk_size = disk_size;
	superblock.disk_blockcount = disk_blockcount;
	memcpy(&superblock, buf, sizeof(struct superblock)); // assuming sizeof superblock < BLOCKSIZE
	if (putblock(0, buf))
		return -1;

	fsync (disk_fd);
	close (disk_fd);

	return -(i < disk_blockcount / 4);
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

	// if already mounted
	if (disk_fd != 0)
		return -1;

	strcpy (disk_name, vdisk);
	disk_fd = open (disk_name, O_RDWR);
	if (disk_fd == -1) {
		// printf ("myfs_mount: disk open error %s\n", disk_name);
		exit(1);
	}

	fstat (disk_fd, &finfo);

	// printf ("myfs_mount: mounting %s, size=%d\n", disk_name,
	// 	(int) finfo.st_size);
	disk_size = (int) finfo.st_size;
	disk_blockcount = disk_size / BLOCKSIZE;

	// perform your mount operations here

	// allocate temporary buffer the size of 1 block, for better copying
	char *buf = malloc(BLOCKSIZE);

	// read superblock into buffer
	if (getblock(0, buf)) {
		// printf("could not read superblock\n");
		free(buf);
		return -1;
	}
	memcpy(&superblock, buf, sizeof(struct superblock)); // assuming sizeof superblock < BLOCKSIZE

	// copy elements of superblock into memory, or simply read global variables from buffer directly
	// superblock elements guaranteed to be the same as global variables, not necessary

	// initialize shared memory
#ifdef shm_open
	sprintf(shm_name, "myfs_%s", disk_name);
	shm_fd = shm_open(shm_name, O_RDWR | O_CREAT, 0666);
	ftruncate(shm_fd, shm_size);
	printf("using shared memory\n");

	// read directory, assuming its size is a little over 1 block
	dir = mmap(0, sizeof(struct dir), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	if (dir == MAP_FAILED) {
		// printf("mapping dir failed\n");
		exit(1);
	}
#else
	dir = malloc(sizeof(struct dir));
#endif

	if (getblock(1, dir) || getblock(2, buf)) {
		// printf("could not read directory table\n");
		free(buf);
		return -1;
	}
	memcpy(((char *) dir) + BLOCKSIZE, buf, sizeof(struct dir) - BLOCKSIZE);

	/*
	// read FAT, assuming it is 16 blocks
	for (int i = 0; i < sizeof(struct fat) / BLOCKSIZE; ++i) {
		if (getblock(2 + i, ((char *) &fat) + i * BLOCKSIZE)) {
			printf("could not read FAT\n");
			free(buf);
			return -1;
		}
	}
	*/

	// initialize open file table
#ifdef shm_open
	opentable = mmap(0, sizeof(struct opentable), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 2*BLOCKSIZE);
	if (opentable == MAP_FAILED) {
		// printf("mapping opentable failed\n");
		exit(1);
	}
#else
	opentable = malloc(sizeof(struct opentable));
#endif
	open_init(opentable);

	free(buf);
  	return (0);
}


int myfs_umount()
{
	// perform your unmount operations here

	if (disk_fd == 0) // already unmounted or not open
		return -1;

	char *buf = malloc(BLOCKSIZE);

	// copy elements of superblock from memory, or simply read global variables from buffer directly
	strcpy(superblock.disk_name, disk_name);
	superblock.disk_size = disk_size;
	superblock.disk_blockcount = disk_blockcount;

	// write superblock into buffer
	memcpy(buf, &superblock, sizeof(struct superblock));
	if (putblock(0, buf)) {
		// printf("could not write superblock\n");
		free(buf);
		return -1;
	}

	// write directory, assuming its size is a little over 1 block
	memcpy(buf, ((char *) dir) + BLOCKSIZE, sizeof(struct dir) - BLOCKSIZE); // should not wiping buffer here matter?
	if (putblock(1, dir) || putblock(2, buf)) {
		// printf("could not write directory table\n");
		free(buf);
		return -1;
	}
#ifndef shm_open
	free(dir);
#endif
	free(buf);

	/*
	// write FAT, assuming it is 16 blocks
	for (int i = 0; i < sizeof(struct fat) / BLOCKSIZE; ++i) {
		if (putblock(2 + i, ((char *) &fat) + i * BLOCKSIZE)) {
			printf("could not write FAT\n");
			return -1;
		}
	}
	*/

#ifdef shm_open
	if (shm_unlink(shm_name)) {
		// printf("unlink failed\n");
		exit(1);
	}
#else
	free(opentable);
#endif

	fsync (disk_fd);
	close (disk_fd);
	disk_fd = 0;
	return (0);
}


/* create a file with name filename */
int myfs_create(char *filename)
{
	// retrieve new FCB
	/* int inum = */ dir_add(dir, filename);
	/*
	if (inum == -1) // file already exists
		return -1;
	// printf("created file %s with fd %d\n", filename, inum);
	return 0;
	*/
	return myfs_open(filename);
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
		// printf("file %s does not exist\n", filename);
		return -1;
	}

	// create new open file table entry for index
	index = open_add(opentable, filename, inum, dir);
	if (index == -1) {
		// printf("cannot open file %s\n", filename);
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
	return open_close(opentable, fd);
}

int myfs_delete(char *filename)
{
	struct inode inode;

	// first check if open
	if (open_isopen(opentable, filename, dir)) {
		// printf("file %s is open\n", filename);
		return -1;
	}

	// retrieve file, checking if it actually exists
	// then remove it from directory entry and read its inode
	int inum = dir_remove(dir, filename, &inode);
	if (inum == -1) {
		// printf("file %s does not exist\n", filename);
		return -1;
	}

	// deallocate data blocks in order
	BLOCKTYPE i = inode.start, j;
	while ((j = fat_getnext(i)) != 0) {
		fat_dealloc(i); // is more needed for deallocation?
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

	if (n > MAXREADWRITE)
		return bytes_read;

	// check if file open
	struct open_entry *entry = open_get(opentable, fd);

	if (entry == NULL || entry->inode->size == 0) // empty file
		return bytes_read;

	// retrieve current block
	// read byte by byte until offset == size or bytes_read == n
	// if current block changes (size / BLOCKSIZE), retrieve new block and update curr

	char *blockbuf = malloc(BLOCKSIZE);
	int siz; // how many bytes to read

	if (getblock(entry->curr, blockbuf)) {
		// printf("reading block %d failed\n", entry->curr);
		free(blockbuf);
		return bytes_read;
	}

	// read byte by byte, reread block if necessary
	bytes_read = 0;
	while (bytes_read < n && entry->offset < entry->inode->size) {
		// copy byte from buffer
		// buf[bytes_read++] = blockbuf[entry->offset++ % BLOCKSIZE];

		// try to read remaining bytes
		siz = n - bytes_read;
		if (siz > entry->inode->size - entry->offset) // will reach EOF
			siz = entry->inode->size - entry->offset;
		if (siz > BLOCKSIZE - entry->offset % BLOCKSIZE) // will reach end of block
			siz = BLOCKSIZE - entry->offset % BLOCKSIZE;

		memcpy(buf + bytes_read, blockbuf + entry->offset % BLOCKSIZE, siz);
		bytes_read += siz;
		entry->offset += siz;

		// change current block if necessary
		if (entry->offset % BLOCKSIZE == 0) { // only execute if will continue
			entry->curr = fat_getnext(entry->curr);
			// printf("next block %d\n", entry->curr);
			if (entry->curr == (BLOCKTYPE) -1 || getblock(entry->curr, blockbuf))
				break;
		}

		// printf("read %d bytes, offset %d, block %d, size %d\n", siz, entry->offset, entry->curr, entry->inode->size);
		// printf("read %s\n", buf + bytes_read);
	}

	/*
	if (bytes_read < n && entry->offset < entry->inode->size)
		printf("reading block %d failed\n", entry->curr);
	*/

	free(blockbuf);
	return (bytes_read) ?: -1; // should return -1 if trying to read after EOF
}

int myfs_write(int fd, void *buf, int n)
{
	int bytes_written = -1;
	int siz;

	if (n > MAXREADWRITE)
		return bytes_written;

	// check if file open
	struct open_entry *entry = open_get(opentable, fd);

	if (entry == NULL)
		return bytes_written;

	// same as read, instead if offset == size and bytes_written < n,
	// increment size and if necessary allocate new block on fat

	// if no blocks, allocate in beginning
	if (entry->inode->size == 0) {
		entry->inode->start = entry->curr = fat_setnext(0);
		if (!entry->inode->start) // no space available
			return bytes_written;
	}

	// current block
	char *blockbuf = malloc(BLOCKSIZE);
	if (getblock(entry->curr, blockbuf)) {
		free(blockbuf);
		return bytes_written;
	}

	bytes_written = 0;
	while (bytes_written < n) {
		// blockbuf[entry->offset++] = buf[bytes_written++];

		// try to write remaining blocks
		siz = n - bytes_written;
		if (siz > BLOCKSIZE - entry->offset % BLOCKSIZE) // will reach end of block
			siz = BLOCKSIZE - entry->offset % BLOCKSIZE;

		memcpy(blockbuf + entry->offset % BLOCKSIZE, buf + bytes_written, siz);
		bytes_written += siz;
		entry->offset += siz;

		if (entry->offset >= entry->inode->size)
			entry->inode->size = entry->offset;
		if (entry->offset % BLOCKSIZE == 0) {
			if (putblock(entry->curr, blockbuf))
				break;
			if (entry->offset == entry->inode->size)
				entry->curr = fat_setnext(entry->curr); // returns 0 if no space left
			else
				entry->curr = fat_getnext(entry->curr);
			// printf("next block %d\n", entry->curr);
			if (entry->curr == 0 || getblock(entry->curr, blockbuf))
				break;
		}

		// printf("written %d bytes, offset %d, block %d, size %d\n", siz, entry->offset, entry->curr, entry->inode->size);
		// printf("written %s\n", buf + bytes_written);
	}

	putblock(entry->curr, blockbuf);
	free(blockbuf);
	return (bytes_written);
}

int myfs_truncate(int fd, int size)
{
	// compare size with current size
	// then seek size in fat
	// deallocate every block after current block in order on fat
	// on current block, just change file size to size

	struct open_entry *entry = open_get(opentable, fd);

	if (entry == NULL || entry->inode->size <= size)
		return -(!entry);

	// traverse fat until curr becomes outside size
	BLOCKTYPE curr = entry->inode->start, temp;
	for (int i = 0; i * BLOCKSIZE < size; ++i) // curr should never be -1 or 0
		curr = fat_getnext(curr);

	// deallocate every block after and including curr
	while (curr != (BLOCKTYPE) -1) { // should never be 0
		temp = fat_getnext(curr);
		if (fat_dealloc(curr))
			return -1;
		curr = temp;
	}

	entry->inode->size = size;
	if (entry->offset > size)
		entry->offset = size;

	return (0);
}


int myfs_seek(int fd, int offset)
{
	int position = -1;

	// traverse fat
	struct open_entry *entry = open_get(opentable, fd);
	if (entry == NULL)
		return position;

	// compare offset with size
	position = offset;
	if (position > entry->inode->size)
		position = entry->inode->size;

	// start from beginning of file
	// entry->offset = 0;
	entry->curr = entry->inode->start;

	// skip blocks before last one
	for (int i = 0; i < position / BLOCKSIZE; ++i) {
		// entry->offset += BLOCKSIZE;
		entry->curr = fat_getnext(entry->curr);
	}
	entry->offset = position; // % BLOCKSIZE;

	return (position);
}

int myfs_filesize (int fd)
{
	int size = -1;

	// retrieve open table entry
	struct open_entry *entry = open_get(opentable, fd);

	if (entry == NULL)
		return size;
	size = entry->inode->size;

	return (size);
}


void myfs_print_dir ()
{
	// linear scan through dir
	for (int i = 0; i < dir->filenum; ++i)
		printf("%s\n", dir->entries[i].filename);
}


void myfs_print_blocks (char *  filename)
{
	// find filename on dir
	// for each file, traverse fat from their start
	int inum = dir_get(dir, filename);

	if (inum == -1) {
		printf("Error: file %s does not exist.\n", filename);
		return;
	}

	BLOCKTYPE curr = dir->fcbs[inum].inode.start;
	printf("%s:", filename);
	while (curr != 0 && curr != (BLOCKTYPE) -1) {
		printf(" %d", curr);
		curr = fat_getnext(curr);
	}
	putchar('\n');
}

// FAT functions

// 2 bytes per FAT block, first 3 blocks for superblock and directory entry
#define FATBLOCK(blk)  ((blk) / (BLOCKSIZE / 2) + 3)
#define FATOFFSET(blk) ((blk) % (BLOCKSIZE / 2))

// size of FAT in blocks
#define FATSIZE (BLOCKCOUNT * 2 / BLOCKSIZE)

// may cache the following with buffers, written after each set

BLOCKTYPE fat_getnext(BLOCKTYPE blk)
{
	// read block in FAT
	BLOCKTYPE *buf = malloc(BLOCKSIZE);
	if (getblock(FATBLOCK(blk), buf)) {
		free(buf);
		return 0; // used only for unallocated blocks anyway
	}
	BLOCKTYPE res = buf[FATOFFSET(blk)];
	// printf("%d: disk[%d][%d] = %d\n", blk, FATBLOCK(blk), FATOFFSET(blk), res);
	free(buf);
	return res;
}

BLOCKTYPE fat_setnext(BLOCKTYPE blk)
{
	BLOCKTYPE *buf = malloc(BLOCKSIZE);

	// search for free space in current block, else jump to another block of FAT
	BLOCKTYPE newblk = blk ?: BLOCKCOUNT/3, // perhaps pick a more random default quantity
	          res = 0;
	while (!res) {
		// searches linearly for next block of same file, else leaps to 5x+1 where x is the current block in data region
		// 5 is coprime with 3*BLOCKCOUNT/4 = 24K, the number of blocks dedicated to file data
		// x -> px+1 mod q is bijective for (p,q) = 1 and (p-1) | q and provides good separation
		// In fact, one can probably show x -> 5x+1 mod 24K will tour all integers mod 24K much like x -> x+1 mod 24K will,
		//  by noting that the nth iteration of the function takes x to 5^n x + (5^n-1)/(5-1) mod 24K
		//  and then showing by induction that 2^k divides (5^n-1)/(5-1) (hence (5^n-1)/(5-1)*(4x+1)) iff 2^k divides n
		newblk = BLOCKCOUNT/4 + ((1+4*!blk) * (newblk - BLOCKCOUNT/4) + 1) % (3*BLOCKCOUNT/4);
		if (newblk == blk) // went full circle, no free space
			break;

		// first check if load unnecessary
		if (getblock(FATBLOCK(newblk), buf))
			break;
		if (buf[FATOFFSET(newblk)] == 0) {
			res = newblk;
			buf[FATOFFSET(newblk)] = -1; // allocated but not yet used
			if (putblock(FATBLOCK(newblk), buf))
				res = -1; // signifies error
			else if (blk) {
				if (getblock(FATBLOCK(blk), buf)) {
					res = -1;
				} else {
					buf[FATOFFSET(blk)] = newblk;
					if (putblock(FATBLOCK(blk), buf))
						res = -1; // 6 indents, sorry Linus
				}
			}
		}
	}

	free(buf);
	return res;
}

int fat_dealloc(BLOCKTYPE blk)
{
	// read block in FAT
	BLOCKTYPE *buf = malloc(BLOCKSIZE);
	if (getblock(FATBLOCK(blk), buf)) {
		free(buf);
		return -1;
	}

	buf[FATOFFSET(blk)] = 0;

	if (putblock(FATBLOCK(blk), buf)) {
		free(buf);
		return -1;
	}

	free(buf);
	return 0;
}
