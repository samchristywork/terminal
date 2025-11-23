#ifndef TOKENIZE_H
#define TOKENIZE_H

#include <stdbool.h>

#include "terminal.h"

void token_repr(const Term_Token *t, char *buf, int bufsize);
void add_token(Term_Tokens *tokens, Term_TokenType type, const char *value,
               int start_index, int length);
bool is_csi_code(const char *text, int length, int index, int *code_length);
bool is_osc_sequence(const char *text, int length, int index, int *seq_length);
bool matches(const char *text, int length, int index, const char *pattern,
             int *pattern_length);
bool starts_with(const char *str, int length, const char *prefix);
bool ends_with(const char *str, int length, char suffix);
Term_Tokens *tokenize(const char *text, int length);
void free_tokens(Term_Tokens *tokens);
void print_token(Term_Token t);

#endif
