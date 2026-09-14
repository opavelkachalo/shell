#ifndef _H_EDITLINE_SENTRY_
#define _H_EDITLINE_SENTRY_

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


#endif
