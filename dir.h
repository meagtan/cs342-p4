#include <stdint.h>

#include "myfs.h"

/*
 * Auxiliary functions for directory table
 */

#define BLOCKTYPE uint16_t

struct dir {
	struct dir_entry {
		char filename[MAXFILENAMESIZE];
		int inum; // index of fcb in table // TODO BLOCKTYPE defined in myfs.c
	} entries[MAXFILECOUNT];
	struct fcb_entry {
		uint8_t valid;
		struct inode {
			int size;
			BLOCKTYPE start; // index of first data block
		} inode;
	} fcbs[MAXFILECOUNT];
	int filenum;
};



void dir_init(struct dir *);

// returns inum
int dir_get(struct dir *, char *filename);

// size 0, return inum
int dir_add(struct dir *, char *filename);

// doesn't delete blocks, does invalidate fcb
int dir_remove(struct dir *, char *filename);
