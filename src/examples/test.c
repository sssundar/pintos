#include <stdio.h>
#include <syscall.h>

int main(int argc, char *argv[]) {
  bool success = true;
  printf("hiiii. argc = %d, argv = %p\n", argc, argv);
  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
