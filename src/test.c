#include <stdio.h>

#include "terminal.h"

void test(Terminal *terminal, const char *test_name, const char *input) {
  printf("=== %s ===\n", test_name);
  write_string(terminal, input);
  print_terminal(terminal);
  printf("\n");
}

void reset_terminal(Terminal *t) {
  write_string(t, "\x1b[0m\x1b[2J\x1b[H");
}

void basic_tests(Terminal *t) {
  reset_terminal(t);
  test(t, "Normal text", "Hello, World!\n");
  test(t, "Carriage return", "Hello,\rWorld!\n");
}

void basic_color_tests(Terminal *t) {
  reset_terminal(t);
  test(t, "Red text", "\x1b[31mThis is red text\x1b[0m\n");
  test(t, "Red and blue text", "\x1b[31mRed \x1b[34mBlue\x1b[0m Normal\n");
  test(t, "Red background", "\x1b[41mRed background\x1b[0m\n");
  test(t, "Blue on red", "\x1b[34;41mBlue on red\x1b[0m\n");
  test(t, "Bold blue on red", "\x1b[1;34;41mBold blue on red\x1b[0m\n");
}

void advanced_color_tests(Terminal *t) {
  reset_terminal(t);
  test(t, "256 color", "\x1b[38;5;82m256 color green text\x1b[0m\n");
  test(t, "256 orange background", "\x1b[48;5;208m256 color orange background\x1b[0m\n");
  test(t, "256 blue on 256 orange", "\x1b[38;5;21m\x1b[48;5;208m256 blue on 256 orange\x1b[0m\n");
  test(t, "Color types", "\x1b[31mDefault Red\x1b[0m\n\x1b[91mBright Red\x1b[0m\n\x1b[1;31mBold Red\x1b[0m\n\x1b[38;5;196m256 Red\x1b[0m\n");
  test(t, "Background color types", "\x1b[41mDefault Red BG\x1b[0m\n\x1b[101mBright Red BG\x1b[0m\n\x1b[1;41mBold Red BG\x1b[0m\n\x1b[48;5;196m256 Red BG\x1b[0m\n");
}

void attribute_tests(Terminal *t) {
  reset_terminal(t);
  test(t, "Bold", "\x1b[1mThis is bold text\x1b[0m\n");
  test(t, "Reverse", "\x1b[7mReverse text\x1b[0m\n");
  test(t, "Bold, underline, reverse", "\x1b[1mBold \x1b[0m\x1b[4mUnderline\x1b[0m \x1b[7mReverse\x1b[0m\n");
}

void erase_tests(Terminal *t) {
  reset_terminal(t);
  test(t, "Setup", "Hello, World!\nHello, World!\nHello, World!\n");
  test(t, "Erase to end of line", "\x1b[2;7H\x1b[K");
  test(t, "Erase to start of line", "\x1b[1;7H\x1b[1K");
  test(t, "Erase entire line", "\x1b[3;7H\x1b[2K");
  test(t, "Setup", "\x1b[HHello, World!\nHello, World!\nHello, World!\n");
  test(t, "Erase down", "\x1b[2;7H\x1b[J");
  test(t, "Setup", "\x1b[HHello, World!\nHello, World!\nHello, World!\n");
  test(t, "Erase up", "\x1b[2;7H\x1b[1J");
}

void cursor_tests(Terminal *t) {
  reset_terminal(t);
  test(t, "Setup", " <\n <\n <\n\n  > <\n\n> <");
  test(t, "Home", "\x1b[Hx");
  test(t, "Home 2", "\x1b[fo");
  test(t, "Move to row 2", "\x1b[2Ho");
  test(t, "Move to row 3", "\x1b[3fo");
  test(t, "Move to 5,4", "\x1b[5;4Ho");
  test(t, "Move to 7,2", "\x1b[7;2fo");
}

void alt_screen_tests(Terminal *t) {
  reset_terminal(t);
  test(t, "Main Screen", "Hello, Main Screen!\n");
  test(t, "Switch to alt screen", "\x1b[?1049hHello, Alt Screen!\n");
  test(t, "Switch back to main screen", "\x1b[?1049l");
  test(t, "Switch to alt screen again", "\x1b[?1049h");
}

void tab_tests(Terminal *t) {
  reset_terminal(t);
  test(t, "Tabs", "Col1\tCol2\tCol3\nData1\tData2\tData3\n");
}

int main() {
  Terminal t;
  init_terminal(&t, 30, 10);

  basic_tests(&t);
  basic_color_tests(&t);
  advanced_color_tests(&t);
  attribute_tests(&t);
  erase_tests(&t);
  cursor_tests(&t);
  alt_screen_tests(&t);
  tab_tests(&t);
}
