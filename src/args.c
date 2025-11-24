#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"

static void strip(char *s) {
  int len = strlen(s);
  while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                     s[len - 1] == '\n' || s[len - 1] == '\r'))
    s[--len] = '\0';
}

static char *ltrim(char *s) {
  while (*s == ' ' || *s == '\t')
    s++;
  return s;
}

static void load_config(Args *args) {
  const char *home = getenv("HOME");
  if (!home)
    return;

  char path[512];
  snprintf(path, sizeof(path), "%s/.config/terminal/config", home);

  FILE *f = fopen(path, "r");
  if (!f)
    return;

  char line[512];
  while (fgets(line, sizeof(line), f)) {
    char *p = ltrim(line);
    if (*p == '#' || *p == '\0' || *p == '\n')
      continue;

    char *eq = strchr(p, '=');
    if (!eq)
      continue;

    *eq = '\0';
    char *key = p;
    strip(key);
    char *val = ltrim(eq + 1);
    strip(val);
    if (!*key || !*val)
      continue;

    if (strcmp(key, "font-size") == 0) {
      int v = atoi(val);
      if (v > 0)
        args->font_size = v;
    } else if (strcmp(key, "scrollback") == 0) {
      int v = atoi(val);
      if (v > 0)
        args->scrollback = v;
    } else if (strcmp(key, "margin") == 0) {
      int v = atoi(val);
      if (v >= 0)
        args->margin = v;
    } else if (strcmp(key, "font") == 0) {
      args->font = strdup(val);
    } else if (strcmp(key, "fg") == 0) {
      args->fg = strtol(val, NULL, 16);
    } else if (strcmp(key, "bg") == 0) {
      args->bg = strtol(val, NULL, 16);
    } else if (strcmp(key, "log-file") == 0) {
      args->log_file = strdup(val);
    } else if (strncmp(key, "color", 5) == 0) {
      int n = atoi(key + 5);
      if (n >= 0 && n <= 15)
        args->palette[n] = strtol(val, NULL, 16);
    } else if (strcmp(key, "alpha") == 0) {
      int v = atoi(val);
      if (v >= 0 && v <= 255)
        args->alpha = v;
    }
  }
  fclose(f);
}

void print_usage(const char *program_name) {
  fprintf(stderr, "Usage: %s [OPTIONS]\n", program_name);
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  --font-size SIZE      Set font size (default: 14)\n");
  fprintf(stderr,
          "  --scrollback N        Scrollback buffer size (default: 1000)\n");
  fprintf(
      stderr,
      "  --font PATTERN        Fontconfig font pattern (e.g. 'Monospace')\n");
  fprintf(stderr, "  --fg RRGGBB           Default foreground color (hex, "
                  "default: ffffff)\n");
  fprintf(stderr, "  --bg RRGGBB           Default background color (hex, "
                  "default: 000000)\n");
  fprintf(stderr, "  --color N RRGGBB      Override palette color N (0-15) "
                  "with hex value\n");
  fprintf(stderr,
          "  --log-file FILE       Write logs to FILE instead of stdout\n");
  fprintf(
      stderr,
      "  --margin N            Set window margin in pixels (default: 10)\n");
  fprintf(stderr, "  --alpha N             Window opacity 0-255 (default: 255, "
                  "requires compositor)\n");
  fprintf(stderr, "  --help                Show this help message\n");
}

void parse_args(int argc, char *argv[], Args *args) {
  args->font_size = 14;
  args->scrollback = 1000;
  args->log_file = NULL;
  args->font = NULL;
  args->fg = -1;
  args->bg = -1;
  args->margin = 10;
  args->alpha = 255;
  for (int i = 0; i < 16; i++)
    args->palette[i] = -1;

  load_config(args);

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--scrollback") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: --scrollback requires an argument\n");
        print_usage(argv[0]);
        exit(1);
      }
      args->scrollback = atoi(argv[++i]);
      if (args->scrollback <= 0) {
        fprintf(stderr, "Error: scrollback must be positive\n");
        exit(1);
      }
    } else if (strcmp(argv[i], "--font-size") == 0) {
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
      free(args->font);
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
      free(args->log_file);
      args->log_file = argv[++i];
    } else if (strcmp(argv[i], "--margin") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: --margin requires an argument\n");
        print_usage(argv[0]);
        exit(1);
      }
      args->margin = atoi(argv[++i]);
      if (args->margin < 0) {
        fprintf(stderr, "Error: margin must be non-negative\n");
        exit(1);
      }
    } else if (strcmp(argv[i], "--alpha") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: --alpha requires an argument\n");
        print_usage(argv[0]);
        exit(1);
      }
      args->alpha = atoi(argv[++i]);
      if (args->alpha < 0 || args->alpha > 255) {
        fprintf(stderr, "Error: alpha must be 0-255\n");
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
