#include "kernel/types.h"
#include "user/user.h"


int main(int argc, char *argv[]) {
  int secs;

  if (argc != 2) {
    fprintf(2, "usage: sleep secs");
    exit(1);
  }

  if ((secs = atoi(argv[1])) == 0) {
    fprintf(2, "sleep: invalid seconds");
    exit(1);
  }

  sleep(secs);
  exit(0);
}