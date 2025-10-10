#include "kernel/stat.h"
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *args[]) {
  if (argc != 2) {
    fprintf(2, "Usage: sleep for ticks...");
    exit(1);
  }
  int n = atoi(args[1]);
  pause(n);
  exit(0);
}
