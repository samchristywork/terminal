#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"

void print_usage(const char *program_name) {
  fprintf(stderr, "Usage: %s [OPTIONS]\n", program_name);
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  --font-size SIZE      Set font size (default: 14)\n");
  fprintf(stderr, "  --font PATTERN        Fontconfig font pattern (e.g. 'Monospace')\n");
  fprintf(stderr, "  --fg RRGGBB           Default foreground color (hex, default: ffffff)\n");
  fprintf(stderr, "  --bg RRGGBB           Default background color (hex, default: 000000)\n");
  fprintf(stderr, "  --color N RRGGBB      Override palette color N (0-15) with hex value\n");
  fprintf(stderr, "  --log-file FILE       Write logs to FILE instead of stdout\n");
  fprintf(stderr, "  --help                Show this help message\n");
}

void parse_args(int argc, char *argv[], Args *args) {
  args->font_size = 14;
  args->log_file = NULL;
  args->font = NULL;
  args->fg = -1;
  args->bg = -1;
  for (int i = 0; i < 16; i++) args->palette[i] = -1;

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
    } else if (strcmp(argv[i], "--font") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: --font requires an argument\n");
        print_usage(argv[0]);
        exit(1);
      }
      args->font = argv[++i];
    } else if (strcmp(argv[i], "--fg") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: --fg requires an argument\n");
        print_usage(argv[0]);
        exit(1);
      }
      args->fg = strtol(argv[++i], NULL, 16);
    } else if (strcmp(argv[i], "--bg") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: --bg requires an argument\n");
        print_usage(argv[0]);
        exit(1);
      }
      args->bg = strtol(argv[++i], NULL, 16);
    } else if (strcmp(argv[i], "--color") == 0) {
      if (i + 2 >= argc) {
        fprintf(stderr, "Error: --color requires two arguments\n");
        print_usage(argv[0]);
        exit(1);
      }
      int n = atoi(argv[++i]);
      if (n < 0 || n > 15) {
        fprintf(stderr, "Error: --color index must be 0-15\n");
        exit(1);
      }
      args->palette[n] = strtol(argv[++i], NULL, 16);
    } else if (strcmp(argv[i], "--log-file") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: --log-file requires an argument\n");
        print_usage(argv[0]);
        exit(1);
      }
      args->log_file = argv[++i];
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
