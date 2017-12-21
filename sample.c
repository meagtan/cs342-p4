#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "myfs.h"

#define SAMPLEDISK   "sampledisk"
#define SAMPLEFILE   "samplefile"
#define EXPECTEDFILE "expected"
#define SAMPLETEXT   "What a cool project.\nCS 342 rocks 8)\n"

/* Error Handling */

int Creat(const char *path)
{
  int fd;

  if ((fd = creat(path, 0664)) == -1) {
    perror("Cannot create file");
    exit(1);
  }

  return fd;
}

ssize_t Write(int fd, const void* buf, size_t count)
{
  ssize_t result;

  if ((result = write(fd, buf, count)) != count) {
    perror("Error in write.");
    exit(1);
  }

  return result;
}

int Close(int fd)
{
  int result;

  if ((result = close(fd)) != 0) {
    perror("Cannot close file");
    exit(1);
  }

  return result;
}

/* Main */

int main(int argc, char *argv[])
{
  char buf[MAXREADWRITE+1];
  int fd, n;

  /* Create a regular expected output file */
  fd = Creat(EXPECTEDFILE);
  Write(fd, SAMPLETEXT, strlen(SAMPLETEXT));
  Close(fd);

  /* Error handling is omitted for simplicity */
//  myfs_diskcreate(SAMPLEDISK);
//  myfs_makefs(SAMPLEDISK);
  myfs_mount(SAMPLEDISK);

//  myfs_create(SAMPLEFILE);
//  fd = myfs_open(SAMPLEFILE);
//  myfs_write(fd, SAMPLETEXT, strlen(SAMPLETEXT));
//  myfs_close(fd);

  /* As an exercise, read the file from another program */
  fd = myfs_open(SAMPLEFILE);
  n = myfs_read(fd, buf, MAXREADWRITE);
  myfs_close(fd);

  myfs_umount();
  
  buf[n] = '\0';		/* Null terminate string */
  fprintf(stderr, "%s", buf);	/* Print to stderr to distinguish from debug messages of myfs */

  /* stderr output should now be the same as the contents of expected output file */

  /* 
     Contents of buf could have been compared directly to SAMPLETEXT.
     Writing to a file and stderr is done for demonstration purposes.
  */

  return 0;
}
