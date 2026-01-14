#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

enum {
    word_init_size   = 4,
    line_init_size   = 16,
    code_succ        = 0,
    code_quot_msmtch = 1,
};

#define SELF_NAME "shell"

struct word_item {
    char *word;
    struct word_item *next;
};

void wlist_append(struct word_item **first, struct word_item **last,
                  char *word)
{
    struct word_item *tmp;
    tmp = malloc(sizeof(*tmp));
    tmp->word = word;
    tmp->next = NULL;
    if(*last)
        (*last)->next = tmp;
    else
        *first = tmp;
    *last = tmp;
}

int wlist_len(struct word_item *wlist)
{
    int len;
    struct word_item *tmp;
    len = 0;
    for(tmp = wlist; tmp; tmp = tmp->next)
        len++;
    return len;
}

void wlist_free(struct word_item *wlist)
{
    while(wlist) {
        struct word_item *tmp = wlist;
        wlist = wlist->next;
        free(tmp->word);
        free(tmp);
    }
}

char *dstr_init(int *pos, int *size, int initsize)
{
    *pos = 0;
    *size = initsize;
    return malloc(*size);
}

void dstr_append(char **dstr, char c, int *pos, int *size)
{
    if(*pos == *size) {
        *size *= 2;
        *dstr = realloc(*dstr, *size);
    }
    (*dstr)[*pos] = c;
    (*pos)++;
}

int is_whitespace(char c)
{
    return c == ' ' || c == '\t';
}

void add_word(char **word, int *pos, int *size, struct word_item **first,
              struct word_item **last)
{
    dstr_append(word, '\0', pos, size);
    wlist_append(first, last, *word);
}

struct word_item *tokenize_line(char *line, int *status)
{
    char *c, *word;
    int wsize, wpos, in_quots = 0, is_word = 0, escaped = 0;
    struct word_item *wlist = NULL, *last = NULL;
    word = dstr_init(&wpos, &wsize, word_init_size);
    for(c = line; *c; c++) {
        if(is_whitespace(*c) && !in_quots && is_word) {
            is_word = 0;
            add_word(&word, &wpos, &wsize, &wlist, &last);
            word = dstr_init(&wpos, &wsize, word_init_size);
            continue;
        }
        if(*c == '\\' && !escaped) {
            if(c[1] == '\\' || c[1] == '"') {
                escaped = 1;
                continue;
            }
        }
        if(*c == '"' && !escaped) {
            if(!is_word && c[1] == '"' && (c[2] == ' ' || c[2] == '\0')) {
                is_word = 1;
                c++;
                continue;
            }
            in_quots = !in_quots;
            continue;
        }
        if(!is_word && (!is_whitespace(*c) || in_quots))
            is_word = 1;
        if(is_word)
            dstr_append(&word, *c, &wpos, &wsize);
    }
    if(is_word)
        add_word(&word, &wpos, &wsize, &wlist, &last);
    *status = in_quots ? code_quot_msmtch : code_succ;
    return wlist;
}

void print_prompt(FILE *filein, FILE *fileout)
{
    if(filein == stdin)
        fputs("> ", fileout);
}

void close_prompt(FILE *filein, FILE *fileout)
{
    if(filein == stdin)
        fputc('\n', fileout);
}

void print_words(struct word_item *wlist, FILE *fileout)
{
    struct word_item *item;
    for(item = wlist; item; item = item->next)
        fprintf(fileout, "[%s]\n", item->word);
}

char **wlist2arr(struct word_item *wlist)
{
    int len, i;
    char **arr;
    struct word_item *tmp;

    len = wlist_len(wlist) + 1;  /* +1 is for the NULL at the end */
    if(len == 1)
        return NULL;
    arr = malloc(len * sizeof(*arr));
    tmp = wlist;
    for(i = 0; i < len-1; i++) {
        arr[i] = tmp->word;
        tmp = tmp->next;
    }
    arr[len-1] = NULL;
    return arr;
}

char *builtins[] = {
    "cd",
    /* more builtin commands to come... */
};

int is_builtin(const char *cmd)
{
    int blen, i;
    blen = sizeof(builtins) / sizeof(*builtins);
    for(i = 0; i < blen; i++)
        if(0 == strcmp(builtins[i], cmd))
            return 1;
    return 0;
}

int len_argv(char **argv)
{
    char **arg;
    for(arg = argv; *arg; arg++)
        {}
    return arg - argv;
}

void cd(char **argv)
{
    int res, len;
    char *path, *old_path;
    len = len_argv(argv);
    if(len > 2) {
        fprintf(stderr, "%s: cd: too many arguments\n", SELF_NAME);
        return;
    } else if(len == 2) {
        if(0 == strcmp(argv[1], "-")) {
            path = getenv("OLDPWD");
            if(!path) {
                fprintf(stderr, "%s: cd: OLDPWD not set\n", SELF_NAME);
                return;
            }
        } else
            path = argv[1];
    } else {
        path = getenv("HOME");
        if(!path) {
            fprintf(stderr, "%s: cd: HOME not set\n", SELF_NAME);
            return;
        }
    }
    old_path = getenv("PWD");
    res = chdir(path);
    if(res == -1) {
        fprintf(stderr, "%s: cd: %s: %s\n", SELF_NAME, path,
                strerror(errno));
        return;
    }
    setenv("OLDPWD", old_path, 1);
}

void run_builtin(char **argv)
{
    if(0 == strcmp(argv[0], "cd")) {
        cd(argv);
    }
    /* more builtin commands to come... */
}

void exec_cmd(struct word_item *wlist)
{
    int pid;
    char **cmd;
    if(!wlist)
        return;
    cmd = wlist2arr(wlist);
    if(is_builtin(cmd[0])) {
        run_builtin(cmd);
        return;
    }
    pid = fork();
    if(pid == 0) {
        execvp(cmd[0], cmd);
        if(errno == ENOENT)
            fprintf(stderr, "%s: %s: command not found\n",
                    SELF_NAME, cmd[0]);
        else
            perror(cmd[0]);
        exit(69);
    }
    wait(NULL);
    free(cmd);
}

void print_error_msg(int status)
{
    switch(status) {
    case code_quot_msmtch:
        fprintf(stderr, "Error: unmatched quotes\n");
        break;
    default:
        break;
    }
}

void read_lines(FILE *filein, FILE *fileout)
{
    int c, pos, size;
    char *line;
    struct word_item *wlist;
    line = dstr_init(&pos, &size, line_init_size);
    wlist = NULL;
    print_prompt(filein, fileout);
    while((c = fgetc(filein)) != EOF) {
        if(c == '\n') {
            int status;
            dstr_append(&line, '\0', &pos, &size);
            wlist = tokenize_line(line, &status);
            if(status == code_succ)
                /* print_words(wlist, fileout); */
                exec_cmd(wlist);
            else
                print_error_msg(status);
            wlist_free(wlist);
            pos = 0;
            print_prompt(filein, fileout);
            continue;
        }
        dstr_append(&line, c, &pos, &size);
    }
    close_prompt(filein, fileout);
    free(line);
}

int main()
{
    read_lines(stdin, stdout);
    return 0;
}
