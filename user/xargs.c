#include "kernel/param.h"
#include "kernel/types.h"
#include "user/user.h"

int pargc;
char *args[MAXARG + 1], *cmd;

void run(char *buf, char *end) {
  int i;
  char *bufp = buf;
  for (i = pargc - 1; i < MAXARG; i++) {
    args[i] = bufp;
    for (; bufp != end && *bufp != ' '; bufp++);
    if (bufp == end)
      break;
    *bufp = 0;
    bufp++;
  }

  if (bufp != end) {
    fprintf(2, "xargs: too many args\n");
    exit(1);
  }
  *end = 0;
  args[i + 1] = 0;

  exec(cmd, args);
}

int main(int argc, char **argv) {
  if (argc == 1) {
    fprintf(2, "usage: xargs [cmd]\n");
    exit(1);
  }

  if (argc - 1 > MAXARG) {
    fprintf(2, "xargs: too many args\n");
    exit(1);
  }

  pargc = argc;
  cmd = argv[1];
  for (int i = 1; i < argc; i++) {
    args[i - 1] = argv[i];
  }

  char buf[256], *p = buf;

  while (read(0, p, 1)) {
    if (*p == '\n') {
      if (fork() == 0) {
        run(buf, p);
      } else {
        p = buf;
      }
    }

    p++;
  }

  while (wait(0) != -1);
  exit(0);
}