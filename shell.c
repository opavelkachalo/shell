#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>

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
            } else {
                in_quots = !in_quots;
            }
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
    if(status)
        *status = in_quots ? code_quot_msmtch : code_succ;
    if(!wlist.first)
        free(dword.str);
    return wlist.first;
}

char **wlist2arr(struct word_item *wlist, const int *wlen)
{
    int len, i;
    char **arr;
    struct word_item *tmp;
    len = wlist_len(wlist) + 1;  /* +1 is for the NULL at the end of arr */
    if(wlen && *wlen + 1 < len)
        len = *wlen + 1;
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
    "exit",
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
        } else {
            path = argv[1];
        }
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
    setenv("PWD", path, 1);
}

int str_to_int(const char *str, int *ok)
{
    int res = 0, sign = 0;
    const char *p;
    if(*str == '-') {
        sign = 1;
        p = str + 1;
    } else {
        p = str;
    }
    for(; *p; p++) {
        if(*p < '0' || *p > '9') {
            if(ok)
                *ok = 0;
            return 0;
        }
        /* no checks for overflow */
        res = res * 10 + *p - '0';
    }
    if(ok)
        *ok = 1;
    return sign ? -res : res;
}

void exit_cmd(char **argv)
{
    /* there is a minor memory leak caused by this function
     * (wlist and cmd are not being released)
     * but it's not a big deal, because program finishes here anyway */
    int code, len, ok;
    code = 0;
    len = len_argv(argv);
    if(len > 2) {
        fprintf(stderr, "%s: exit: too many arguments\n", SELF_NAME);
        return;
    } else if(len == 2) {
        code = str_to_int(argv[1], &ok);
        if(!ok) {
            fprintf(stderr, "%s: exit: %s: numeric argument required\n",
                    SELF_NAME, argv[1]);
            return;
        }
    }
    exit(code);
}

void run_builtin(char **argv)
{
    if(0 == strcmp(argv[0], "cd")) {
        cd(argv);
    } else if(0 == strcmp(argv[0], "exit")) {
        exit_cmd(argv);
    }
    /* more builtin commands to come... */
}

struct cmd_props {
    int run_in_bg;      /* raised if is a background job (`&' token) */
    int append_f;       /* raised if there is a `>>' token in cmd */
    int redir_in_cnt;   /* count of `<' tokens in cmd */
    int redir_out_cnt;  /* count of `>' tokens in cmd */
    int is_pipeline;    /* raised if there is a `|' token in cmd */
    char *filein;       /* file to redirect stdin to */
    char *fileout;      /* file to redirect stdout to */
    int *fds;           /* array of file descriptors for pipelines */
    char ***cmds;       /* array of cmd arrays if there is a pipeline */
    int size, capacity; /* dynamic array fields fo `cmds' */
};

void cmdp_init(struct cmd_props *cmdp)
{
    cmdp->run_in_bg     = 0;
    cmdp->append_f      = 0;
    cmdp->redir_in_cnt  = 0;
    cmdp->redir_out_cnt = 0;
    cmdp->is_pipeline   = 0;
    cmdp->filein        = NULL;
    cmdp->fileout       = NULL;
    cmdp->fds           = NULL;
    cmdp->cmds          = NULL;
    cmdp->size          = 0;
    cmdp->capacity      = 0;
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
    if(fdcopy_ptr) {
        *fdcopy_ptr = dup(stdfd);
        if(*fdcopy_ptr == -1) {
            perror("dup");
            return -1;
        }
    }
    if(dup2(fd, stdfd) == -1) {
        perror("dup2");
        if(fdcopy_ptr)
            *fdcopy_ptr = -1;
        return -1;
    }
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
        return -1;
    if(cmdp->fileout)
        res = redirect_stdio_stream(1, cmdp->fileout, fdcopy_ptr1,
                                    cmdp->append_f);
    return res;
}

void restore_streams(struct cmd_props *cmdp, int fdcopy0, int fdcopy1)
{
    if(cmdp->filein && fdcopy0 != -1) {
        dup2(fdcopy0, 0);
        close(fdcopy0);
    }
    if(cmdp->fileout && fdcopy1 != -1) {
        dup2(fdcopy1, 1);
        close(fdcopy1);
    }
}

void remove_zombies(int s)
{
    int p;
    signal(SIGCHLD, remove_zombies);
    do {
        p = wait4(-1, NULL, WNOHANG, NULL);
    } while(p > 0);
}

void wait_fg_process(int pid)
{
    int p;
    do {
        p = wait(NULL);
    } while(p != pid && p != -1);
}

void exec_in_subproc(char **cmd)
{
    if(is_builtin(cmd[0])) {
        /* TODO: proper exit codes */
        run_builtin(cmd);
        exit(0);
    }
    execvp(cmd[0], cmd);
    if(errno == ENOENT)
        fprintf(stderr, "%s: %s: command not found\n",
                SELF_NAME, cmd[0]);
    else
        perror(cmd[0]);
    exit(69);
}

void run_cmd(char **cmd, struct cmd_props *cmdp)
{
    int pid, cp0, cp1;
    if(redirect_streams(cmdp, &cp0, &cp1) == -1)
        goto restore;
    if(!cmdp->run_in_bg && is_builtin(cmd[0])) {
        run_builtin(cmd);
        goto restore;
    }
    pid = fork();
    if(pid == -1) {
        perror(SELF_NAME);
        goto restore;
    } else if(pid == 0) {
        exec_in_subproc(cmd);
    }
    if(!cmdp->run_in_bg) {
        signal(SIGCHLD, SIG_DFL);
        wait_fg_process(pid);
        signal(SIGCHLD, remove_zombies);
    }
restore:
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

void delete_word_item(struct word_item **pcur, int free_word)
{
    struct word_item *tmp;
    tmp = *pcur;
    if(free_word)
        free(tmp->word);
    *pcur = tmp->next;
    free(tmp);
}

int handle_bg_token(struct cmd_props *cmdp, struct word_item **pcur)
{
    if(!(*pcur)->next) {
        cmdp->run_in_bg = 1;
        delete_word_item(pcur, 1);
        return 0;
    }
    fprintf(stderr, "`&' must be the last symbol in the line\n");
    return -1;
}

int handle_redirect_token(struct cmd_props *cmdp, struct word_item **pcur)
{
    if(!(*pcur)->next || (*pcur)->next->t_type != token_word) {
        fprintf(stderr, "File name expected after `%s'\n", (*pcur)->word);
        return -1;
    }
    add_redir_info_to_cmdprops(cmdp, *pcur);
    if(cmdp->redir_in_cnt >= 2 || cmdp->redir_out_cnt >= 2) {
        fprintf(stderr, "Ambigous redirection\n");
        return -1;
    }
    /* delete the token itself and the filename, going after it */
    delete_word_item(pcur, 1);
    delete_word_item(pcur, 0);
    return 0;
}

void cmds_append(struct cmd_props *cmdp, char **cmd)
{
    if(cmdp->size == cmdp->capacity) {
        if(cmdp->size == 0)
            cmdp->capacity = 1;
        else
            cmdp->capacity *= 2;
        cmdp->cmds = realloc(cmdp->cmds, sizeof(*cmdp->cmds) * cmdp->capacity);
    }
    (cmdp->cmds)[cmdp->size] = cmd;
    (cmdp->size)++;
}

int handle_pipe_token(struct cmd_props *cmdp, struct word_item **pcur,
                      struct word_item **sub_cmd_p, int *rel_pos)
{
    char **cmd;
    if(!(*pcur)->next || (*pcur)->next->t_type != token_word) {
        fprintf(stderr, "Syntax error near unexpected token `|'\n");
        return -1;
    }
    cmdp->is_pipeline = 1;
    cmd = wlist2arr(*sub_cmd_p, rel_pos);
    if(!cmd) {
        fprintf(stderr, "Syntax error near unexpected token `|'\n");
        return -1;
    }
    cmds_append(cmdp, cmd);

    delete_word_item(pcur, 1);
    *sub_cmd_p = *pcur;
    *rel_pos = 0;
    return 0;
}

int analyze_expression(struct word_item **wlist, struct cmd_props *cmdp)
{
    int res, rel_pos = 0;
    struct word_item *sub_cmd_p = NULL;
    struct word_item **pcur = wlist;
    sub_cmd_p = *wlist;
    while(*pcur) {
        char *cur_word = (*pcur)->word;
        if((*pcur)->t_type != token_delimiter) {
            pcur = &(*pcur)->next;
            rel_pos++;
            continue;
        }
        if(*cur_word == '&') {
            res = handle_bg_token(cmdp, pcur);
        } else if(*cur_word == '<' || *cur_word == '>') {
            res = handle_redirect_token(cmdp, pcur);
        } else if(*cur_word == '|' && cur_word[1] != '|') {
            res = handle_pipe_token(cmdp, pcur, &sub_cmd_p, &rel_pos);
        } else {
            fprintf(stderr, "Feature is not implemented yet\n");
            return -1;
        }
        if(res == -1)
            return -1;
    }
    if(cmdp->is_pipeline) {
        char **cmd = wlist2arr(sub_cmd_p, &rel_pos);
        cmds_append(cmdp, cmd);
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

void free_cmds(char ***cmds, int size)
{
    int i;
    for(i = 0; i < size; i++) {
        free(cmds[i]);
    }
    free(cmds);
}

void print_cmds(char ***cmds, int size)
{
    int i;
    printf("%d\n", size);
    for(i = 0; i < size; i++) {
        char **cmd;
        printf("[ ");
        for(cmd = cmds[i]; *cmd; cmd++) {
            printf("%s ", *cmd);
        }
        printf("]\n");
    }
}

int pipe_n_times(struct cmd_props *cmdp)
{
    int i, len, res;
    len = (cmdp->size - 1) * 2;  /* x2 for output and input fds */
    cmdp->fds = malloc(sizeof(*cmdp->fds) * len);
    for(i = 0; i < len; i+=2) {
        res = pipe(cmdp->fds + i);
        if(res == -1) {  /* exceeded the limit for descriptors amount */
            int j;
            perror("pipe");
            for(j = 0; j < i; j++)
                close(cmdp->fds[j]);
            return -1;
        }
    }
    return 0;
}

void close_all_fds(struct cmd_props *cmdp)
{
    int i, len;
    len = (cmdp->size - 1) * 2;
    for(i = 0; i < len; i++)
        close(cmdp->fds[i]);
}

void run_pipeline_member(struct cmd_props *cmdp, int i)
{
    if(i == 0) {  /* first member */
        if(cmdp->filein)
            redirect_stdio_stream(0, cmdp->filein, NULL, cmdp->append_f);
        dup2(cmdp->fds[1], 1);
    } else if(i == cmdp->size-1) {  /* last member */
        dup2(cmdp->fds[(i-1)*2], 0);
        if(cmdp->fileout)
            redirect_stdio_stream(1, cmdp->fileout, NULL, cmdp->append_f);
    } else {
        dup2(cmdp->fds[(i-1)*2], 0);
        dup2(cmdp->fds[i*2+1], 1);
    }
    close_all_fds(cmdp);
    if(is_builtin(*(cmdp->cmds[i]))) {
        /* TODO: proper exit codes */
        run_builtin(cmdp->cmds[i]);
        exit(0);
    }
    execvp(*(cmdp->cmds[i]), cmdp->cmds[i]);
    perror(*(cmdp->cmds[i]));
    exit(33);
}

int arr_contains(int *arr, int size, int elem)
{
    int i;
    for(i = 0; i < size; i++) {
        if(arr[i] == elem)
            return 1;
    }
    return 0;
}

void wait_pipeline_members(int *pids, int pids_size, int pids_left)
{
    int res;
    signal(SIGCHLD, SIG_DFL);
    while(pids_left > 0) {
        res = wait(NULL);
        if(res == -1)
            break;
        if(arr_contains(pids, pids_size, res))
            pids_left--;
    }
    signal(SIGCHLD, remove_zombies);
}

int run_pipeline(struct cmd_props *cmdp)
{
    int res, i, *pids;
    pids = malloc(sizeof(*pids) * cmdp->size);
    res = pipe_n_times(cmdp);
    if(res == -1)
        goto end_pipeline;
    for(i = 0; i < cmdp->size; i++) {
        res = fork();
        if(res == -1) {
            perror("fork");
            break;
        } else if(res == 0) {
            run_pipeline_member(cmdp, i);
        }
        pids[i] = res;
    }
    close_all_fds(cmdp);
    if(!cmdp->run_in_bg)
        wait_pipeline_members(pids, cmdp->size, i);
end_pipeline:
    free(pids);
    return res;
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
    if(cmdp.is_pipeline) {
        run_pipeline(&cmdp);
        goto cleanup;
    }
    if(!*wlist) {
        /* truncate file if cmd is `>file' */
        if(cmdp.fileout && !cmdp.append_f)
            make_empty_file(cmdp.fileout);
        goto cleanup;
    }
    cmd = wlist2arr(*wlist, NULL);
    run_cmd(cmd, &cmdp);
cleanup:
    free(cmdp.filein);
    free(cmdp.fileout);
    free_cmds(cmdp.cmds, cmdp.size);
    free(cmdp.fds);
    free(cmd);
}

/* TODO: use isatty(3) in the future for `prompt' functions */

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
            /* TODO: env variables expansion; `*`, `?` patterns matching */ 
            wlist = tokenize_line(dline.str, &status);
            if(status == code_succ && wlist)
                eval(&wlist);
            else
                print_error_msg(status);
            if(wlist)
                wlist_free(wlist);
            dline.pos = 0;
            print_prompt(filein, fileout);
            continue;
        }
        dstr_append(&dline, c);
    }
    close_prompt(filein, fileout);
    free(dline.str);
}

int main()
{
    signal(SIGCHLD, remove_zombies);
    read_lines(stdin, stdout);
    return 0;
}
