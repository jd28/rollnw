#include <iostream>
#include <miniunz.h>

void printUsage() {
  std::cerr << "usage: test-unzip <src> [out.zip]" << std::endl;
}

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    printUsage();
    exit(EXIT_FAILURE);
  }

  unzip(argv[1], argv[2]);
  return 0;
}
