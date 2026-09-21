#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

#include "containers.h"
#include "editline.h"

static int term_width, term_height;
static struct winsize wsize;

#define DEBUG_PRINT
#ifdef DEBUG_PRINT

static int dbg_msg_len;
static char dbg_str[16];

static void debug_print(const char *msg, int msg_len)
{
    static int prev_len = 0;
    int len_to_clear;
    if(!term_width)
        return;
    len_to_clear = msg_len >= prev_len ? msg_len : prev_len;
    GOTO_NTH_COL(term_width - len_to_clear);
    printf("%*s", len_to_clear, "");
    GOTO_NTH_COL(term_width - msg_len);
    printf("%s", msg);
    prev_len = msg_len;
}
#endif /* DEBUG_PRINT */

enum key {
    ctrl_a = 1,
    ctrl_b,
    ctrl_c,
    ctrl_d,
    ctrl_e,
    ctrl_f,
    ctrl_h = 8,
    ctrl_i,          /* horizontal tab */
    ctrl_j,          /* line feed ('\n') */
    ctrl_k,          /* kill chars after curpos */
    ctrl_l,          /* clear the screen */
    ctrl_n = 14,
    ctrl_p = 16,
    ctrl_u = 21,
    ctrl_w = 23,
    escape = 27,
    backspace = 127,
    /*
     * TODO: alt_f  -- word forward
     * TODO: alt_b  -- word back
     * TODO: alt_u  -- uppercase
     * TODO: alt_l  -- lowercase
     * TODO: alt_c  -- capitalize
     * TODO: alt_t  -- swap words
     * TODO: ctrl_t -- swap chars
     */
};

static void jump_start(struct l_list *line)
{
    GOTO_NTH_COL(1);
    line->curp = NULL;
    line->curpos = 1;
}

static void jump_end(struct l_list *line)
{
    GOTO_NTH_COL(line->size + 1);
    line->curp = line->tail;
    line->curpos = line->size + 1;
}

static void move_back(struct l_list *line)
{
    if(!line->curp)
        return;
    CURSOR_BACK(1);
    line->curp = line->curp->prev;
    line->curpos--;
}

static void move_forward(struct l_list *line)
{
    if(line->curp != line->tail) {
        CURSOR_FORWARD(1);
        if(line->curp)
            line->curp = line->curp->next;
        else
            line->curp = line->head;
        line->curpos++;
    }
}

static void rewrite_line(struct l_list *line)
{
    CLEAR_N_CHARS(line->size);
    l_list_print(line);
}

static void del_char(struct l_list *line)
{
    CLEAR_N_CHARS(line->size);
    l_delete(line);
    l_list_print(line);
    GOTO_NTH_COL(line->curpos);
}

static void bs_char(struct l_list *line)
{
    if(!line->curp)
        return;
    CLEAR_N_CHARS(line->size);
    l_backspace(line);
    l_list_print(line);
    GOTO_NTH_COL(line->curpos);
}

int is_ws(char c)
{
    return c == ' ' || c == '\n' || c == '\t';
}

static void werase(struct l_list *line)
{
    int i, n_to_del = 0;
    struct l_item *tmp;

    for(tmp = line->curp; tmp && is_ws(tmp->val); tmp = tmp->prev)
        n_to_del++; /* first non-ws char */
    for(; tmp && !is_ws(tmp->val); tmp = tmp->prev)
        n_to_del++;
    for(i = 0; i < n_to_del; i++)
        bs_char(line);
}

static void erase_line(struct l_list *line)
{
    CLEAR_N_CHARS(line->size);
    l_list_free(line);
    l_list_init(line);
    GOTO_NTH_COL(1);
}

/* 
  special delimiter means that after typing it, user expects the command
  to be autocompleted rather than a filename
*/
static int is_spec_delim(char c)
{
    return c == '&' || c == '|' || c == ';' || c == '(';
}

int is_delimiter(char c)
{
    return c == '&' || c == '>' || c == '<' || c == '|' || c == ';' ||
           c == '(' || c == ')';
}

static int is_first_word(struct l_list *line)
{
    struct l_item *cur;
    int cur_word_ended = 0;

    for(cur = line->curp; cur; cur = cur->prev) {
        if(!cur_word_ended) {
            if(is_ws(cur->val) || is_delimiter(cur->val))
                cur_word_ended = 1;
        }
        if(cur_word_ended) {
            if(is_spec_delim(cur->val))
                break;
            if(!is_ws(cur->val))
                return 0;
        }
    }
    return 1;
}

static char *get_cur_word(struct l_list *line)
{
    struct l_item *cur;
    struct d_str word;
    int i;

    DA_SET_TO_ZERO(&word);
    for(cur = line->curp; cur; cur = cur->prev) {
        if(is_ws(cur->val) || is_delimiter(cur->val)) {
            break;
        }
        DA_APPEND(&word, cur->val);
    }
    for(i = 0; i < word.size / 2; i++) {
        char tmp = word.items[i];
        word.items[i] = word.items[word.size-i-1];
        word.items[word.size-i-1] = tmp;
    }
    DA_APPEND(&word, '*');
    DA_APPEND(&word, '\0');
    return word.items;
}

static int str_contains(const char *str, char c)
{
    const char *p;
    if(str) {
        for(p = str; *p; p++) {
            if(*p == c)
                return 1;
        }
    }
    return 0;
}

static int match(const char *str, const char *pat)
{
    int i;
    if(!str || !pat)
        return 0;
    for(;; str++, pat++) {
        switch(*pat) {
        case 0:
            return *str == 0;
        case '*':
            for(i = 0; ; i++) {
                if(match(str+i, pat+1))
                    return 1;
                if(!str[i])
                    return 0;
            }
        case '?':
            if(!*str)
                return 0;
            break;
        default:
            if(*str != *pat)
                return 0;
        }
    }
}

static int is_dir(const char *path)
{
    int res;
    struct stat sb;
    res = lstat(path, &sb);
    if(res == -1)
        return 0;
    return (sb.st_mode & S_IFMT) == S_IFDIR;
}

static int is_dot_or_ddot(const char *path)
{
    return (path[0] == '.' && path[1] == '\0') ||
           (path[0] == '.' && path[1] == '.' && path[2] == '\0');
}

static char *str_concat(const char *s1, const char *s2)
{
    int len1, len2;
    char *res;
    len1 = strlen(s1);
    len2 = strlen(s2);
    res = calloc(len1 + len2 + 1, sizeof(*res));
    memcpy(res, s1, len1);
    memcpy(res+len1, s2, len2);
    return res;
}

static char *str_dup_ext(const char *s, int slen, int extlen)
{
    char *res;
    res = calloc(slen + extlen, sizeof(*res));
    memcpy(res, s, slen);
    return res;
}

static void search_matches(const char *where, const char *word,
                           struct str_arr *matches)
{
    int len;
    char *match_name, *full_path;
    DIR *dirp;
    struct dirent *dent;

    dirp = opendir(where);
    if(!dirp)
        return;
    while((dent = readdir(dirp)) != NULL) {
        if(*word != '.' && is_dot_or_ddot(dent->d_name))
            continue;
        if(match(dent->d_name, word)) {
            len = strlen(dent->d_name);
            match_name = str_dup_ext(dent->d_name, len, 2);
            full_path = str_concat(where, match_name);
            if(is_dir(full_path))
                match_name[len] = '/';
            free(full_path);
            DA_APPEND(matches, match_name);
        }
    }
    str_arr_sort(matches, 1);
    closedir(dirp);
}

/* `out' should be initialized */
static void split_str(const char *str, char by, struct str_arr *out)
{
    const char *p;
    struct d_str substr;

    if(!str)
        return;
    DA_SET_TO_ZERO(&substr);
    for(p = str; ; p++) {
        if(!*p || *p == by) {
            DA_APPEND(&substr, '\0');
            DA_APPEND(out, substr.items);
            if(!*p)
                break;
            DA_SET_TO_ZERO(&substr);
            continue;
        }
        DA_APPEND(&substr, *p);
    }
}

static char *get_path(const char *word)
{
    char *res, *pos, *p;
    res = strdup(word);
    pos = res;
    for(p = res; *p; p++) {
        if(*p == '/')
            pos = p;
    }
    if(pos == res && *pos != '/') {
        *pos = '.';
        pos += 1;
        *pos = '/';
    }
    pos[1] = '\0';
    return res;
}

static void complete_chars(struct l_list *line, int idx, const char *match)
{
    const char *p;
    for(p = match + idx; *p; p++) {
        l_append(line, *p);
    }
}

static void complete_n_chars(struct l_list *line, int idx, int n,
                             const char *match)
{
    int i;
    for(i = idx; i < idx + n; i++) {
        l_append(line, match[i]);
    }
}

static int common_substr(const char *word, struct str_arr *matches)
{
    int i, j, n_common;

    n_common = 0;
    for(i = strlen(word)-1; ; i++) {
        char c = matches->items[0][i];
        for(j = 1; j < matches->size; j++) {
            if(matches->items[j][i] != c)
                return n_common;
        }
        n_common++;
    }
}

static void print_matches(struct str_arr *matches)
{
    int i;
    putchar('\n');
    for(i = 0; i < matches->size; i++)
        printf("%s\n", matches->items[i]);
}

static void chop_path(char *word, char *path)
{
    int plen, wlen;
    plen = strlen(path);
    wlen = strlen(word);
    memmove(word, word+plen, wlen-plen+1);
}

static void autocomplete(struct l_list *line)
{
    char *word;
    struct str_arr places, matches;
    int i;

    DA_SET_TO_ZERO(&places);
    DA_SET_TO_ZERO(&matches);
    word = get_cur_word(line);
    if(is_first_word(line) && !str_contains(word, '/')) {
        split_str(getenv("PATH"), ':', &places);
        /* TODO: add builtins to the mathes array */
    } else {
        char *path = get_path(word);
        if(0 != strcmp(path, "./") || (word[0] == '.' && word[1] == '/'))
            chop_path(word, path);
        DA_APPEND(&places, path);
    }
    for(i = 0; i < places.size; i++) {
        search_matches(places.items[i], word, &matches);
    }
    if(matches.size == 1) {
        complete_chars(line, strlen(word)-1, matches.items[0]);
    } else if(matches.size > 1) {
        int res = common_substr(word, &matches);
        if(res)
            complete_n_chars(line, strlen(word)-1, res, matches.items[0]);
        else
            print_matches(&matches);
    }
    rewrite_line(line);
    free(word);
    str_arr_free(&matches);
    str_arr_free(&places);
}

struct history hist = {0};

static void read_hist_file()
{
    int c;
    struct d_str line;
    FILE *f;

    DA_SET_TO_ZERO(&line);
    f = fopen(hist.hist_path, "r");
    if(!f)
        return;
    while((c = fgetc(f)) != EOF) {
        if(c == '\n') {
            DA_APPEND(&line, '\0');
            DA_APPEND(&hist.list, line.items);
            DA_SET_TO_ZERO(&line);
            continue;
        }
        DA_APPEND(&line, c);
    }
    fclose(f);
}

void hist_init(const char *hist_path, int lines_limit)
{
    hist.available = 1;
    hist.hist_path = hist_path;
    hist.lines_limit = lines_limit;
    if(hist.hist_path)
        read_hist_file();
    hist.cur_idx = hist.list.size;
}

static void write_history_file()
{
    int i, i_0;
    FILE *f;

    f = fopen(hist.hist_path, "w");
    if(!f)
        return;
    i_0 = hist.list.size > hist.lines_limit ?
          hist.list.size - hist.lines_limit : 0;
    for(i = 0; i < hist.lines_limit; i++) {
        if(i_0 + i == hist.list.size)
            break;
        fprintf(f, "%s\n", hist.list.items[i_0+i]);
    }
    fclose(f);
}

void hist_close()
{
    if(hist.hist_path)
        write_history_file();
    str_arr_free(&hist.list);
}

static void history_next(struct l_list *line)
{
    char *line_str;
    struct l_list *next_line;

    if(!hist.available || hist.cur_idx == hist.list.size)
        return;
    line_str = l_list_to_str(line);
    free(hist.list.items[hist.cur_idx]);
    hist.list.items[hist.cur_idx] = line_str;
    CLEAR_N_CHARS(line->size);
    l_list_free(line);
    hist.cur_idx++;
    next_line = str_to_l_list(hist.list.items[hist.cur_idx]);
    memcpy(line, next_line, sizeof(*next_line));
    free(next_line);
    l_list_print(line);
}

static void history_prev(struct l_list *line)
{
    char *line_str;
    struct l_list *prev_line;

    if(!hist.available || hist.cur_idx == 0)
        return;
    line_str = l_list_to_str(line);
    if(hist.cur_idx == hist.list.size && !hist.cycled_history) {
        DA_APPEND(&hist.list, line_str);
        hist.list.size--;
        hist.cycled_history = 1;
    } else {
        free(hist.list.items[hist.cur_idx]);
        hist.list.items[hist.cur_idx] = line_str;
    }
    CLEAR_N_CHARS(line->size);
    l_list_free(line);
    hist.cur_idx--;
    prev_line = str_to_l_list(hist.list.items[hist.cur_idx]);
    memcpy(line, prev_line, sizeof(*prev_line));
    free(prev_line);
    l_list_print(line);
}

void history_add(char *str)
{
    if(!hist.available)
        return;
    if(hist.cycled_history) {
        free(hist.list.items[hist.list.size]);
        hist.cycled_history = 0;
    }
    DA_APPEND(&hist.list, strdup(str));
    hist.cur_idx = hist.list.size;
}

static struct termios saveset, curset;

static void term_raw()
{
    tcgetattr(0, &saveset);
    memcpy(&curset, &saveset, sizeof(saveset));
    curset.c_lflag &= ~(ICANON | ISIG | ECHO);
    curset.c_cc[VMIN] = 1;
    curset.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &curset);
}

static void term_restore()
{
    tcsetattr(0, TCSANOW, &saveset);
}

static void process_esc_seq(struct l_list *line)
{
    char c;

    getchar();  /* read '[' */
    c = getchar();
    switch(c) {
    case 'A':  /* (up    arrow) = ^[[A */
        history_prev(line);
        break;
    case 'B':  /* (down  arrow) = ^[[B */
        history_next(line);
        break;
    case 'C':  /* (right arrow) = ^[[C */
        move_forward(line);
        break;
    case 'D':  /* (left  arrow) = ^[[D */
        move_back(line);
        break;
    default:
    }
}

static char *edit_line()
{
    int c;
    struct l_list line;
    char *res;

    term_raw();
    l_list_init(&line);
    for(;;) {
#ifdef DEBUG_PRINT
        dbg_msg_len = sprintf(dbg_str, "%c %d",
                line.curp ? line.curp->val : '~',
                line.curpos);
        if(dbg_msg_len >= 0) {
            debug_print(dbg_str, dbg_msg_len);
            GOTO_NTH_COL(line.curpos);
        }
#endif
        c = getchar();
        switch(c) {
        case ctrl_c:
            printf("^%c\n", 'A'+c-1);
            res = calloc(1, 1); /* res = "" */
            goto end;
        case ctrl_a:
            jump_start(&line);
            break;
        case ctrl_e:
            jump_end(&line);
            break;
        case ctrl_b:
            move_back(&line);
            break;
        case ctrl_f:
            move_forward(&line);
            break;
        case ctrl_h:
        case backspace:
            bs_char(&line);
            break;
        case ctrl_d:
            if(line.size == 0) {
                res = NULL;
                goto end;
            } else {
                del_char(&line);
            }
            break;
        case ctrl_u:
            erase_line(&line);
            break;
        case ctrl_w:
            werase(&line);
            break;
        case ctrl_i:
            autocomplete(&line);
            break;
        case ctrl_j: /* '\n' */
            putchar(c);
            res = l_list_to_str(&line);
            goto end;
        case ctrl_n:
            history_next(&line);
            break;
        case ctrl_p:
            history_prev(&line);
            break;
        case escape:
            process_esc_seq(&line);
            break;
        default:
            l_append(&line, c);
            if(line.curp == line.tail) {
                putchar(c);
            } else {
                rewrite_line(&line);
            }
        }
    }
end:
    l_list_free(&line);
    term_restore();
    return res;
}

char *get_line()
{
    int c;
    struct d_str str;

    if(isatty(0)) {
        if(ioctl(0, TIOCGWINSZ, &wsize) == 0) {
            term_width = wsize.ws_col;
            term_height = wsize.ws_row;
        }
        return edit_line();
    }
    DA_SET_TO_ZERO(&str);
    while((c = getchar()) != EOF) {
        if(c == '\n') {
            DA_APPEND(&str, '\0');
            return str.items;
        }
        DA_APPEND(&str, c);
    }
    return NULL;
}
/* TODO: prompt */
/* TODO: history */
/* TODO: multiline strings */
/* TODO: SIGWINCH handling */
/* TODO: erasing chars with some keys should copy them to the internal
         clipboard buffer */
