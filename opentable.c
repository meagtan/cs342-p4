#include "opentable.h"

#include <stdlib.h>
#include <string.h>

/*
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
};
*/

void open_init(struct opentable *open)
{
	memset(open, 0, sizeof(struct opentable));
}

int open_add(struct opentable *open, char *filename, BLOCKTYPE inum, struct dir *dir)
{
	// find free entry
	if (open->minfree == -1)
		return -1;

	// fill entry
	int fd = open->minfree;
	struct open_entry *entry = &open->entries[fd];
	entry->valid = 1;
	memcpy(entry->filename, filename, MAXFILENAMESIZE); // should it be strcpy?
	entry->inum = inum;
	entry->offset = 0;

	// printf("added inode %d to open table with fd %d\n", inum, open->minfree);

	// link fcb
	entry->inode = &dir->fcbs[inum].inode;
	entry->curr  = entry->inode->start;

	// update filenum, minfree
	open->filenum++;
	int i = (open->minfree + 1) % MAXOPENFILES;
	while (open->entries[i].valid && i != open->minfree)
		i = (i + 1) % MAXOPENFILES;
	if (i == open->minfree) // looped through entire list
		open->minfree = -1;
	else
		open->minfree = i;

	// printf("new minfree %d\n", open->minfree);

	open->counts[inum]++;

	return fd;
}

int open_close(struct opentable *open, int fd)
{
	if (!open->entries[fd].valid)
		return -1;

	open->entries[fd].valid = 0;
	open->filenum--;
	open->counts[open->entries[fd].inum]--;
	if (open->minfree == -1 || open->minfree > fd)
		open->minfree = fd;
	return 0;
}

int open_isopen(struct opentable *open, char *filename, struct dir *dir)
{
	/*
	for (int i = 0; i < MAXOPENFILES; ++i)
		if (open->entries[i].valid && !strcmp(open->entries[i].filename, filename))
			return 1;

	return 0;
	*/

	return open->counts[dir_get(dir, filename)];
}

struct open_entry *open_get(struct opentable *open, int fd)
{
	struct open_entry *entry = &open->entries[fd];
	return entry->valid ? entry : NULL;
}


