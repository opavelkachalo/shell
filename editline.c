#include "editline.h"
#include <stdio.h>
#include <termios.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

/* #include <assert.h> */

#ifdef DEBUG_PRINT
# include <sys/ioctl.h>

static int term_width;
static char dbg_str[16];
static int dbg_msg_len;
static struct winsize w;

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
#endif

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
    ctrl_n = 14,
    ctrl_p = 16,
    ctrl_w = 23,
    backspace = 127,
};

struct l_item {
    char val;
    struct l_item *next;
    struct l_item *prev;
};

/*
 * before doing anything with l_list,
 * it *must* be initialized with `l_list_init`
 */
struct l_list {
    struct l_item *head;
    struct l_item *tail;
    struct l_item *curp;
    int size;
    int curpos;
    int nth_word;
};

static void append_tmp_to_tail(struct l_list *list, struct l_item *tmp)
{
    tmp->next = NULL;
    tmp->prev = list->tail;
    if(list->tail)
        list->tail->next = tmp;
    else
        list->head = tmp;
    list->tail = tmp;
}

static void insert_tmp_before_head(struct l_list *list, struct l_item *tmp)
{
    tmp->next = list->head;
    tmp->prev = NULL;
    if(list->head)
        list->head->prev = tmp;
    else
        list->tail = tmp;
    list->head = tmp;
}

static void append_tmp_to_curp(struct l_list *list, struct l_item *tmp)
{
    tmp->next = list->curp->next;
    tmp->prev = list->curp;
    if(list->curp->next)
        list->curp->next->prev = tmp;
    else
        list->tail = tmp;
    list->curp->next = tmp;
}

static void l_append(struct l_list *list, char val)
{
    struct l_item *tmp;
    tmp = malloc(sizeof(*tmp));
    tmp->val = val;
    if(list->curp == list->tail) {
        append_tmp_to_tail(list, tmp);
    } else if(!list->curp) {
        insert_tmp_before_head(list, tmp);
    } else {
        append_tmp_to_curp(list, tmp);
    }
    list->curp = tmp;
    list->size++;
    list->curpos++;
}

static void l_delete(struct l_list *list)
{
    struct l_item *tmp;
    if(!list->curp && list->head) {
        tmp = list->head;
        list->head = tmp->next;
        if(!list->head)
            list->tail = list->head;
        else
            list->head->prev = NULL;
        /* list->curp = list->head; */
    } else if(!list->curp || !list->curp->next) {
        return;
    } else {
        tmp = list->curp->next;
        list->curp->next = tmp->next;
        if(tmp->next)
            tmp->next->prev = list->curp;
        else
            list->tail = list->curp;
    }
    free(tmp);
    list->size--;
}

static void l_backspace(struct l_list *list)
{
    struct l_item *tmp = list->curp;
    if(!tmp)
        return;
    list->curp = tmp->prev;
    list->curpos--;
    l_delete(list);
}

static void l_list_print(struct l_list *list)
{
    struct l_item *tmp;
    for(tmp = list->head; tmp; tmp = tmp->next)
        putchar(tmp->val);
}

static char *l_list_to_str(struct l_list *list)
{
    int i;
    char *str;
    struct l_item *tmp;

    i = 0;
    str = calloc(list->size + 1, sizeof(*str));
    for(tmp = list->head; tmp; tmp = tmp->next) {
        str[i] = tmp->val;
        i++;
    }
    return str;
}

static void l_list_init(struct l_list *list)
{
    list->head = NULL;
    list->tail = NULL;
    list->curp = NULL;
    list->size = 0;
    list->curpos = 1;
}

static void l_list_free(struct l_list *list)
{
    struct l_item *tmp;
    while(list->head) {
        tmp = list->head;
        list->head = list->head->next;
        free(tmp);
    }
}

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

static int is_ws(char c)
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

static void autocomplete(struct l_list *line)
{
   if(line->nth_word == 1) {

   } else {

   }
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
        case EOF:
            res = NULL;
            goto end;
        case ctrl_d:
            if(line.size == 0) {
                res = NULL;
                goto end;
            } else {
                del_char(&line);
            }
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
            /* history next */
            break;
        case ctrl_p:
            /* history prev */
            break;
        default:
            l_append(&line, c);
            if(line.curp == line.tail) {
                putchar(c);
            } else {
                CLEAR_N_CHARS(line.size);
                l_list_print(&line);
                GOTO_NTH_COL(line.curpos);
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
    int c, size, capacity;
    char *res;

    if(isatty(0)) {
#ifdef DEBUG_PRINT
    if(ioctl(0, TIOCGWINSZ, &w) == 0)
        term_width = w.ws_col;
#endif
        return edit_line();
    }
    size = 0;
    capacity = 0;
    res = NULL;
    while((c = getchar()) != EOF) {
        if(c == '\n')
            c = '\0';
        if(size == capacity) {
            if(size == 0)
                capacity = 1;
            else
                capacity *= 2;
            res = realloc(res, capacity);
        }
        res[size] = c;
        size++;
    }
    return res;
}

int main()
{
    char *str;

    str = get_line();
    if(str)
        printf("[%s]\n", str);
    free(str);
    return 0;
}
/* TODO: autocompletion */
/* TODO: history */
/* TODO: multiline strings */
/* TODO: SIGWINCH handling */
