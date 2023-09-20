#include "kernel/types.h"
#include "user/user.h"


int main(int argc, char *argv[]) {
  int fd[4];
  char buf;
  
  if (argc != 1) {
    fprintf(2, "usage: pingpong");
    exit(1);
  }

  pipe(fd);
  pipe(fd + 2);
  if (fork() == 0) {
    read(fd[0], &buf, 1);
    printf("%d: received ping\n", getpid());
    write(fd[3], &buf, 1);
  } else {
    write(fd[1], " ", 1);
    read(fd[2], &buf, 1);
    printf("%d: received pong\n", getpid());
    wait(0);
  }
  exit(0);
}