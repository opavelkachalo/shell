#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

enum {
    word_init_size   = 4,
    line_init_size   = 16,
    code_succ        = 0,
    code_quot_msmtch = 1,
};

#define SELF_NAME "shell"

enum token_type { token_word, token_delimiter };

struct word_item {
    char *word;
    enum token_type t_type;
    struct word_item *next;
};

struct word_list {
    struct word_item *first, *last;
};

void wlist_append(struct word_list *wlist, char *word,
                  enum token_type t_type)
{
    struct word_item *tmp;
    tmp = malloc(sizeof(*tmp));
    tmp->word = word;
    tmp->t_type = t_type;
    tmp->next = NULL;
    if(wlist->last)
        wlist->last->next = tmp;
    else
        wlist->first = tmp;
    wlist->last = tmp;
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

struct dyn_str {
    int pos, size;
    char *str;
};

void dstr_init(struct dyn_str *dstr, int initsize)
{
    dstr->pos = 0;
    dstr->size = initsize;
    dstr->str = malloc(initsize);
}

void dstr_append(struct dyn_str *dstr, char c)
{
    if(dstr->pos == dstr->size) {
        dstr->size *= 2;
        dstr->str = realloc(dstr->str, dstr->size);
    }
    (dstr->str)[dstr->pos] = c;
    (dstr->pos)++;
}

int is_whitespace(char c)
{
    return c == ' ' || c == '\t';
}

void add_word_to_wlist(struct dyn_str *dstr, struct word_list *wlist,
              enum token_type t_type)
{
    dstr_append(dstr, '\0');
    wlist_append(wlist, dstr->str, t_type);
}

int is_delimiter(char c)
{
    return c == '&' || c == '>' || c == '<' || c == '|' || c == ';' ||
           c == '(' || c == ')';
}

int delimiter_len(char *c)
{
    /* 1 or 2
       & > < | ; ( )
       >> && ||
    */
    if(*c == c[1] && (*c == '>' || *c == '&' || *c == '|'))
        return 2;
    return 1;
}

void add_delimiter_to_wlist(char **c, struct dyn_str *dword,
                            struct word_list *wlist)
{
    int dlen, i;
    dlen = delimiter_len(*c);
    /* finish and add current word to the wlist */
    if(dword->pos != 0) {
        add_word_to_wlist(dword, wlist, token_word);
        dstr_init(dword, dlen+1);
    }
    /* copy delimiter to the dword and add it to the wlist */
    for(i = 0; i < dlen; i++)
        dstr_append(dword, (*c)[i]);
    add_word_to_wlist(dword, wlist, token_delimiter);
    /* init another word if it's not the end of line */
    if((*c)[dlen])
        dstr_init(dword, word_init_size);
    (*c) += dlen-1;
}

struct word_item *tokenize_line(char *line, int *status)
{
    char *c;
    struct dyn_str dword;
    int in_quots = 0, is_word = 0, escaped = 0;
    struct word_list wlist = { NULL, NULL };
    dstr_init(&dword, word_init_size);
    for(c = line; *c; c++) {
        if(is_whitespace(*c) && !in_quots && is_word) {
            add_word_to_wlist(&dword, &wlist, token_word);
            dstr_init(&dword, word_init_size);
            is_word = 0;
        } else if(!in_quots && is_delimiter(*c)) {
            add_delimiter_to_wlist(&c, &dword, &wlist);
            is_word = 0;
        } else if(*c == '\\' && !escaped && (c[1] == '\\' || c[1] == '"')) {
            escaped = 1;
        } else if(*c == '"' && !escaped) {
            /* creating an empty word */
            if(!is_word && c[1] == '"' && (c[2] == ' ' || c[2] == '\0')) {
                is_word = 1;
                c++;
            } else
                in_quots = !in_quots;
        } else {
            if(!is_word && (!is_whitespace(*c) || in_quots))
                is_word = 1;
            if(is_word)
                dstr_append(&dword, *c);
            escaped = 0;
        }
    }
    if(is_word)
        add_word_to_wlist(&dword, &wlist, token_word);
    *status = in_quots ? code_quot_msmtch : code_succ;
    if(!wlist.first)
        free(dword.str);
    return wlist.first;
}

void print_prompt(FILE *filein, FILE *fileout)
{
    if(filein == stdin)
        fputs("% ", fileout);
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

struct cmd_props {
    int run_in_bg, append_f, redir_in_cnt, redir_out_cnt;
    char *filein, *fileout;
};

void cmdp_init(struct cmd_props *cmdp)
{
    cmdp->run_in_bg = 0;
    cmdp->append_f = 0;
    cmdp->redir_in_cnt = 0;
    cmdp->redir_out_cnt = 0;
    cmdp->filein = NULL;
    cmdp->fileout = NULL;
}

int redirect_stdio_stream(int stdfd, const char *fname, int *fdcopy_ptr,
                          int append_f)
{
    int fd, flags;
    if(stdfd == 0) {
        flags = O_RDONLY;
    } else {
        flags = O_WRONLY | O_CREAT;
        flags |= append_f ? O_APPEND : O_TRUNC;
    }
    fd = open(fname, flags, 0666);
    if(fd == -1) {
        perror(fname);
        return -1;
    }
    *fdcopy_ptr = dup(stdfd);
    dup2(fd, stdfd);
    close(fd);
    return 0;
}

int redirect_streams(struct cmd_props *cmdp, int *fdcopy_ptr0,
                     int *fdcopy_ptr1)
{
    int res = 0;
    if(cmdp->filein)
        res = redirect_stdio_stream(0, cmdp->filein, fdcopy_ptr0,
                                    cmdp->append_f);
    if(res == -1)
        return res;
    if(cmdp->fileout)
        res = redirect_stdio_stream(1, cmdp->fileout, fdcopy_ptr1,
                                    cmdp->append_f);
    return res;
}

void restore_streams(struct cmd_props *cmdp, int fdcopy0, int fdcopy1)
{
    if(cmdp->filein) {
        dup2(fdcopy0, 0);
        close(fdcopy0);
    }
    if(cmdp->fileout) {
        dup2(fdcopy1, 1);
        close(fdcopy1);
    }
}

void remove_zombies()
{
    int p;
    do {
        p = wait4(-1, NULL, WNOHANG, NULL);
    } while(p > 0);
}

void wait_fg_process(int pid)
{
    int p;
    do {
        p = wait(NULL);
    } while(p != pid);
}

void exec_cmd(char **cmd, struct cmd_props *cmdp)
{
    int pid, cp0, cp1, res;
    if(cmdp->run_in_bg)
        remove_zombies();
    res = redirect_streams(cmdp, &cp0, &cp1);
    if(res == -1)
        return;
    pid = fork();
    if(pid == -1) {
        perror(SELF_NAME);
        return;
    }
    if(pid == 0) {
        execvp(cmd[0], cmd);
        if(errno == ENOENT)
            fprintf(stderr, "%s: %s: command not found\n",
                    SELF_NAME, cmd[0]);
        else
            perror(cmd[0]);
        exit(69);
    }
    if(!cmdp->run_in_bg)
        wait_fg_process(pid);
    restore_streams(cmdp, cp0, cp1);
}

void add_redir_info_to_cmdprops(struct cmd_props *cmdp,
                                struct word_item *cur)
{
    char *cur_word, *redir_file;
    cur_word = cur->word;
    redir_file = cur->next->word;
    if(cur_word[0] == '<') {
        cmdp->filein = redir_file;
        cmdp->redir_in_cnt++;
    } else {
        cmdp->fileout = redir_file;
        cmdp->redir_out_cnt++;
        if(cur_word[1] == '>')
            cmdp->append_f = 1;
    }
}

void delete_word_item(struct word_item **pcur, int free_word_f)
{
    struct word_item *tmp;
    tmp = *pcur;
    if(free_word_f)
        free(tmp->word);
    *pcur = tmp->next;
    free(tmp);
}

int analyze_expression(struct word_item **wlist, struct cmd_props *cmdp)
{
    struct word_item **pcur;
    pcur = wlist;
    while(*pcur) {
        char *s = (*pcur)->word;
        if((*pcur)->t_type != token_delimiter) {
            pcur = &(*pcur)->next;
            continue;
        }
        if(*s == '&') {
            if(!(*pcur)->next) {
                cmdp->run_in_bg = 1;
                delete_word_item(pcur, 1);
            } else {
                fprintf(stderr, "& must be the last symbol in the line\n");
                return -1;
            }
        } else
        if(*s == '<' || *s == '>') {
            if(!(*pcur)->next || (*pcur)->next->t_type != token_word) {
                fprintf(stderr, "File name expected after `%s'\n", s);
                return -1;
            }
            add_redir_info_to_cmdprops(cmdp, *pcur);
            if(cmdp->redir_in_cnt >= 2 || cmdp->redir_out_cnt >= 2) {
                fprintf(stderr, "Ambigous redirection\n");
                return -1;
            }
            delete_word_item(pcur, 1);
            delete_word_item(pcur, 0);
            continue;
        } else {
            fprintf(stderr, "Feature is not implemented yet\n");
            return -1;
        }
        if(*pcur)
            pcur = &(*pcur)->next;
    }
    return 0;
}

void make_empty_file(const char *path)
{
    int fd;
    fd = open(path, O_CREAT|O_TRUNC, 0666);
    if(fd == -1) {
        perror(path);
        return;
    }
    close(fd);
}

void eval(struct word_item **wlist)
{
    int res;
    char **cmd;
    struct cmd_props cmdp;
    cmd = NULL;
    cmdp_init(&cmdp);
    res = analyze_expression(wlist, &cmdp);
    if(res == -1)
        goto cleanup;
    if(!*wlist) {
        /* truncate file if cmd is `>file' */
        if(cmdp.fileout && !cmdp.append_f)
            make_empty_file(cmdp.fileout);
        goto cleanup;
    }
    cmd = wlist2arr(*wlist);
    if(is_builtin(cmd[0])) {
        run_builtin(cmd);
        goto cleanup;
    }
    exec_cmd(cmd, &cmdp);
cleanup:
    if(cmdp.filein)
        free(cmdp.filein);
    if(cmdp.fileout)
        free(cmdp.fileout);
    if(cmd)
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
    int c;
    struct dyn_str dline;
    struct word_item *wlist;
    dstr_init(&dline, line_init_size);
    wlist = NULL;
    print_prompt(filein, fileout);
    while((c = fgetc(filein)) != EOF) {
        if(c == '\n') {
            int status;
            dstr_append(&dline, '\0');
            wlist = tokenize_line(dline.str, &status);
            if(status == code_succ && wlist)
                /* print_words(wlist, fileout); */
                eval(&wlist);
            else
                print_error_msg(status);
            if(wlist)
                wlist_free(wlist);
            dline.pos = 0;
            print_prompt(filein, fileout);
            remove_zombies();
            continue;
        }
        dstr_append(&dline, c);
    }
    close_prompt(filein, fileout);
    free(dline.str);
}

int main()
{
    read_lines(stdin, stdout);
    return 0;
}
