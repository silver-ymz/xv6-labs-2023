#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char **argv) {
  if (argc != 1) {
    fprintf(2, "usage: uptime\n");
    exit(1);
  }

  int seconds, minutes, hours, days;
  seconds = uptime();
  minutes = seconds / 60;
  seconds %= 60;
  hours = minutes / 60;
  minutes %= 60;
  days = hours / 24;
  hours %= 24;

  if (days) {
    printf("%d days %d hours %d minutes %d seconds\n", days, hours, minutes,
           seconds);
  } else if (hours) {
    printf("%d hours %d minutes %d seconds\n", hours, minutes, seconds);
  } else if (minutes) {
    printf("%d minutes %d seconds\n", minutes, seconds);
  } else {
    printf("%d seconds\n", seconds);
  }

  exit(0);
}