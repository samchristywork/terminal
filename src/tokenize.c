#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "terminal.h"
#include "tokenize.h"

void token_repr(const Term_Token *t, char *buf, int bufsize) {
  int out = 0;
  for (int i = 0; i < t->length && out < bufsize - 4; i++) {
    unsigned char c = (unsigned char)t->value[i];
    if (isprint(c)) {
      buf[out++] = c;
    } else {
      int n = snprintf(buf + out, bufsize - out, "\\x%02x", c);
      if (n > 0)
        out += n;
    }
  }
  buf[out] = '\0';
}

void add_token(Term_Tokens *tokens, Term_TokenType type, const char *value,
               int start_index, int length) {
  if (tokens->count % 128 == 0) {
    Term_Token *new_tokens = (Term_Token *)realloc(
        tokens->tokens, (tokens->count + 128) * sizeof(Term_Token));
    if (!new_tokens)
      return;
    tokens->tokens = new_tokens;
  }
  if (length > 255)
    length = 255;
  Term_Token *token = &tokens->tokens[tokens->count++];
  token->type = type;
  token->length = length;
  memcpy(token->value, &value[start_index], length);
  token->value[length] = '\0';
}

bool is_csi_code(const char *text, int length, int index, int *code_length) {
  if (index + 2 < length && text[index] == '\x1b' && text[index + 1] == '[') {
    int i = index + 2;
    while (i < length && (unsigned char)text[i] >= 0x30 &&
           (unsigned char)text[i] <= 0x3f) {
      i++;
    }
    while (i < length && (unsigned char)text[i] >= 0x20 &&
           (unsigned char)text[i] <= 0x2f) {
      i++;
    }
    if (i < length && (unsigned char)text[i] >= 0x40 &&
        (unsigned char)text[i] <= 0x7e) {
      *code_length = i - index + 1;
      return true;
    }
  }
  return false;
}

bool is_osc_sequence(const char *text, int length, int index, int *seq_length) {
  if (index + 2 >= length || text[index] != '\x1b' || text[index + 1] != ']')
    return false;
  for (int i = index + 2; i < length; i++) {
    if ((unsigned char)text[i] == 0x07) {
      *seq_length = i - index + 1;
      return true;
    }
    if (i + 1 < length && text[i] == '\x1b' && text[i + 1] == '\\') {
      *seq_length = i - index + 2;
      return true;
    }
  }
  return false;
}

bool matches(const char *text, int length, int index, const char *pattern,
             int *pattern_length) {
  int pat_len = strlen(pattern);
  if (index + pat_len <= length &&
      strncmp(&text[index], pattern, pat_len) == 0) {
    *pattern_length = pat_len;
    return true;
  }
  return false;
}

bool starts_with(const char *str, int length, const char *prefix) {
  int prefix_len = strlen(prefix);
  if (length < prefix_len) {
    return false;
  }
  return strncmp(str, prefix, prefix_len) == 0;
}

bool ends_with(const char *str, int length, char suffix) {
  if (length < 1) {
    return false;
  }
  return str[length - 1] == suffix;
}

Term_Tokens *tokenize(const char *text, int length) {
  Term_Tokens *tokens = (Term_Tokens *)malloc(sizeof(Term_Tokens));
  tokens->tokens = (Term_Token *)malloc(128 * sizeof(Term_Token));
  tokens->count = 0;

  for (int i = 0; i < length; i++) {
    int len = 0;
    if (is_osc_sequence(text, length, i, &len)) {
      add_token(tokens, TOKEN_OSC, text, i, len);
    } else if (is_csi_code(text, length, i, &len)) {
      add_token(tokens, TOKEN_CSI_CODE, text, i, len);
    } else if (matches(text, length, i, "\n", &len)) {
      add_token(tokens, TOKEN_NEWLINE, text, i, len);
    } else if (matches(text, length, i, "\r", &len)) {
      add_token(tokens, TOKEN_CARRIAGE_RETURN, text, i, len);
    } else if (matches(text, length, i, "\b", &len) ||
               (i < length && (unsigned char)text[i] == 0x7f)) {
      add_token(tokens, TOKEN_BACKSPACE, text, i, 1);
    } else if (matches(text, length, i, "\x1b[J", &len)) {
      add_token(tokens, TOKEN_ERASE_DOWN, text, i, len);
    } else if (matches(text, length, i, "\x1b[0J", &len)) {
      add_token(tokens, TOKEN_ERASE_DOWN, text, i, len);
    } else if (matches(text, length, i, "\x1b[1J", &len)) {
      add_token(tokens, TOKEN_ERASE_UP, text, i, len);
    } else if (matches(text, length, i, "\x1b[2J", &len)) {
      add_token(tokens, TOKEN_ERASE_ALL, text, i, len);
    } else if (matches(text, length, i, "\x1b[3J", &len)) {
      add_token(tokens, TOKEN_ERASE_SCROLLBACK, text, i, len);
    } else if (matches(text, length, i, "\x1b[K", &len)) {
      add_token(tokens, TOKEN_ERASE_EOL, text, i, len);
    } else if (matches(text, length, i, "\x1b[0K", &len)) {
      add_token(tokens, TOKEN_ERASE_EOL, text, i, len);
    } else if (matches(text, length, i, "\x1b[1K", &len)) {
      add_token(tokens, TOKEN_ERASE_SOL, text, i, len);
    } else if (matches(text, length, i, "\x1b[2K", &len)) {
      add_token(tokens, TOKEN_ERASE_LINE, text, i, len);
    } else if (matches(text, length, i, "\x1b[?1049h", &len)) {
      add_token(tokens, TOKEN_ALT_SCREEN, text, i, len);
    } else if (matches(text, length, i, "\x1b[?1049l", &len)) {
      add_token(tokens, TOKEN_MAIN_SCREEN, text, i, len);
    } else if (matches(text, length, i, "\x1b[?25l", &len)) {
      add_token(tokens, TOKEN_CURSOR_HIDE, text, i, len);
    } else if (matches(text, length, i, "\x1b[?25h", &len)) {
      add_token(tokens, TOKEN_CURSOR_SHOW, text, i, len);
    } else if (matches(text, length, i, "\x1b[?2004h", &len)) {
      add_token(tokens, TOKEN_BRACKETED_PASTE_ON, text, i, len);
    } else if (matches(text, length, i, "\x1b[?2004l", &len)) {
      add_token(tokens, TOKEN_BRACKETED_PASTE_OFF, text, i, len);
    } else if (matches(text, length, i,
                       "\x1b"
                       "7",
                       &len)) {
      add_token(tokens, TOKEN_CSI_CODE, text, i, len);
    } else if (matches(text, length, i,
                       "\x1b"
                       "8",
                       &len)) {
      add_token(tokens, TOKEN_CSI_CODE, text, i, len);
    } else if (matches(text, length, i,
                       "\x1b"
                       "M",
                       &len)) {
      add_token(tokens, TOKEN_REVERSE_INDEX, text, i, len);
    } else if (matches(text, length, i,
                       "\x1b"
                       "c",
                       &len)) {
      add_token(tokens, TOKEN_FULL_RESET, text, i, len);
    } else if (i + 1 < length && text[i] == '\x1b' &&
               (text[i + 1] == '=' || text[i + 1] == '>')) {
      len = 2; // application/normal keypad mode
    } else if (i + 2 < length && text[i] == '\x1b' &&
               (text[i + 1] == '(' || text[i + 1] == ')' ||
                text[i + 1] == '*' || text[i + 1] == '+')) {
      len = 3; // character set designation (e.g., \x1b(B)
    } else if (is_osc_sequence(text, length, i, &len)) {
      add_token(tokens, TOKEN_OSC, text, i, len);
    } else if (is_csi_code(text, length, i, &len)) {
      add_token(tokens, TOKEN_CSI_CODE, text, i, len);
    } else if (matches(text, length, i, "\t", &len)) {
      add_token(tokens, TOKEN_TAB, text, i, len);
    } else if (text[i] == '\x07') {
      add_token(tokens, TOKEN_BEL, text, i, 1);
      len = 1;
    } else {
      int start = i;
      while (i < length) {
        unsigned char c = (unsigned char)text[i];
        if (c >= 0x20 && c < 0x7f) {
          i++;
        } else if (c >= 0x80) {
          i++; // multi-byte UTF-8 lead or continuation byte
        } else {
          break;
        }
      }
      len = i - start;
      if (len > 0) {
        for (int k = 0; k < len; k += 255)
          add_token(tokens, TOKEN_TEXT, text, start + k,
                    len - k < 255 ? len - k : 255);
      } else {
        len = 1; // skip unrecognised byte; prevent i from decrementing
      }
      i = start;
    }
    i += len - 1;
  }

  return tokens;
}

void print_token(Term_Token t) {
  printf("%d: ", t.type);
  for (int i = 0; i < t.length; i++) {
    if (isprint(t.value[i])) {
      printf("%c", t.value[i]);
    } else {
      printf("\\x%02x", (unsigned char)t.value[i]);
    }
  }
  printf("\n");
}
