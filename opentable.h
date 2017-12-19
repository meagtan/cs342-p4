#ifndef __OPEN_H
#define __OPEN_H

#include "myfs.h"
#include "dir.h"

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

void open_init(struct opentable *);

int open_add(struct opentable *, char *filename, BLOCKTYPE inum, struct dir *);

int open_close(struct opentable *, int fd);

int open_isopen(struct opentable *, char *filename);

struct open_entry *open_get(struct opentable *, int fd);

#endif
