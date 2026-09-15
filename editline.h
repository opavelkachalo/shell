#ifndef _H_EDITLINE_SENTRY_
#define _H_EDITLINE_SENTRY_

#include "containers.h"

#define GOTO_NTH_COL(n) do { printf("\033[%dG", (n)); } while(0)

#define CLEAR_N_CHARS(n) do {\
    GOTO_NTH_COL(1);\
    printf("%*s", (n), "");\
    GOTO_NTH_COL(1);\
} while(0)

#define CURSOR_BACK(n) do { printf("\033[%dD", (n)); } while(0)
#define CURSOR_FORWARD(n) do { printf("\033[%dC", (n)); } while(0)

char *get_line();
int is_delimiter(char c);
int is_ws(char c);

struct history {
    int available;
    int cur_idx;
    int lines_limit;
    int cycled_history;
    struct str_arr list;
    const char *hist_path;
};

extern struct history hist;
void hist_init(const char *hist_path, int lines_limit);
void hist_close();
void history_add(char *str);

#endif
