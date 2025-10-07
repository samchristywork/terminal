#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"

void print_usage(const char *program_name) {
  fprintf(stderr, "Usage: %s [OPTIONS]\n", program_name);
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  --font-size SIZE    Set font size (default: 14)\n");
  fprintf(stderr, "  --help              Show this help message\n");
}

void parse_args(int argc, char *argv[], Args *args) {
  args->font_size = 14;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--font-size") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: --font-size requires an argument\n");
        print_usage(argv[0]);
        exit(1);
      }
      args->font_size = atoi(argv[++i]);
      if (args->font_size <= 0) {
        fprintf(stderr, "Error: font size must be positive\n");
        exit(1);
      }
    } else if (strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      exit(0);
    } else {
      fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
      print_usage(argv[0]);
      exit(1);
    }
  }
}
