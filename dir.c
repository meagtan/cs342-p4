#include <stdlib.h>
#include <string.h>

#include "dir.h"

int getindex(struct dir *dir, char *filename);

void dir_init(struct dir *dir)
{
	// fill struct with zeros
	memset(&dir, 0, sizeof(dir));
}

int dir_get(struct dir *dir, char *filename)
{
	int i = getindex(dir, filename);
	if (i == -1)
		return -1;
	return dir->entries[i].inum;
}

// size 0
int dir_add(struct dir *dir, char *filename)
{
	if (dir->filenum == MAXFILECOUNT)
		return -1;

	// similar to get, locates predecessor or successor of filename
	int i = 0, j = dir->filenum - 1, k = dir->filenum, cmp = 0;
	while (i <= j) {
		k = (i + j) / 2;
		cmp = strcmp(dir->entries[k].filename, filename);
		if (cmp == 0) return -1; // file already exists
		else if (cmp < 0) i = k + 1;
		else j = k - 1;
	}

	// create new entry, shift succeeding entries
	// k: index of new entry, should be incremented if last element was the predecessor of new file
	if (cmp < 0) k = i;
	for (j = dir->filenum - 1; k <= j; --j)
		dir->entries[j+1] = dir->entries[j]; // struct copy
	strcpy(dir->entries[k].filename, filename);

	// find free entry in FCB table
	for (i = 0; !dir->fcbs[i].valid; ++i);

	// initialize FCB
	dir->entries[k].inum = i;
	fcbs[i].inode.size = fcbs[i].inode.start = 0;

	return k;
}

// doesn't delete blocks
int dir_remove(struct dir *dir, char *filename, struct inode *inode)
{
	int i = getindex(dir, filename);
	if (i == -1)
		return -1;
	int inum = dir->entries[i].inum;
	*inode = dir->fcbs[inum].inode; // struct copy
	dir->fcbs[inum].valid = 0;

	// shift elements after i
	for (; i < dir->filenum - 1; ++i)
		dir->entries[i] = dir->entries[i+1];

	return dir->inum;
}

int getindex(struct dir *dir, char *filename)
{
	int i = 0, j = dir->filenum - 1, k, cmp;
	while (i <= j) {
		k = (i + j) / 2;
		cmp = strcmp(dir->entries[k].filename, filename);
		if (cmp == 0)
			return k;
		else if (cmp < 0)
			i = k + 1;
		else
			j = k - 1;
	}

	return -1;
}
