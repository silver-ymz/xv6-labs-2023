#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  int pipes[4], num, read_pipe, write_pipe;

  if (argc != 1) {
    fprintf(2, "usage: primes");
    exit(1);
  }

  pipe(pipes);
  pipe(pipes + 2);

  int global_read_pipe = pipes[2], global_write_pipe = pipes[3];

  if (fork() == 0) {
  child:
    close(pipes[1]);
    close(global_read_pipe);
    read_pipe = pipes[0];
    if (read(read_pipe, &num, 4) <= 0) {
      close(read_pipe);
      close(global_write_pipe);
      exit(0);
    }

    write(global_write_pipe, &num, 4);

    pipe(pipes);
    if (fork() == 0) {
      close(read_pipe);
      goto child;
    } else {
      int prime_num = num;
      close(global_write_pipe);
      close(pipes[0]);
      write_pipe = pipes[1];
      while (read(read_pipe, &num, 4) > 0) {
        if (num % prime_num != 0) {
          write(write_pipe, &num, 4);
        }
      }
      close(read_pipe);
      close(write_pipe);
      exit(0);
    }
  } else {
    close(pipes[0]);
    close(global_write_pipe);
    write_pipe = pipes[1];
    for (int i = 2; i <= 35; i++) {
      write(write_pipe, &i, 4);
    }
    close(write_pipe);

    while (read(global_read_pipe, &num, 4) > 0) {
      printf("prime %d\n", num);
    }
    while (wait(0) != -1);
  }

  exit(0);
}