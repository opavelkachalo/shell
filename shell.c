#include <stdio.h>
#include <stdlib.h>

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
        (*first) = tmp;
    *last = tmp;
}

void wlist_free(struct word_item *wlist)
{
    struct word_item *tmp;
    while(wlist) {
        tmp = wlist;
        wlist = wlist->next;
        free(tmp->word);
        free(tmp);
    }
}

enum {
    word_init_size   = 4,
    line_init_size   = 16,
    code_succ        = 0,
    code_quot_msmtch = 1,
};

char *dstr_init(int *pos, int *size, int initsize)
{
    *pos = 0;
    *size = initsize;
    return malloc(*size);
}

void dstr_append(char **dstr, char c, int *pos, int *size)
{
    if(*pos >= *size) {
        *size *= 2;
        *dstr = realloc(*dstr, *size);
    }
    (*dstr)[*pos] = c;
    (*pos)++;
}

int is_delimiter(char c)
{
    return c == ' ' || c == '\t';
}

struct word_item *tokenize_line(char *line, int *status)
{
    char *c, *word;
    int wsize, wpos, in_quots, is_word;
    struct word_item *wlist, *last;
    wlist = NULL;
    last = NULL;
    word = dstr_init(&wpos, &wsize, word_init_size);
    in_quots = 0;
    is_word = 0;
    for(c = line; *c; c++) {
        if(is_delimiter(*c) && !in_quots) {
            if(is_word) {
                is_word = 0;
                dstr_append(&word, '\0', &wpos, &wsize);
                wlist_append(&wlist, &last, word);
                word = dstr_init(&wpos, &wsize, word_init_size);
                continue;
            }
        }
        if(*c == '"') {
            in_quots = !in_quots;
            continue;
        }
        if(!is_word && (!is_delimiter(*c) || in_quots))
            is_word = 1;
        if(is_word)
            dstr_append(&word, *c, &wpos, &wsize);
    }
    if(is_word) {
        dstr_append(&word, '\0', &wpos, &wsize);
        wlist_append(&wlist, &last, word);
    }
    if(in_quots)
        *status = code_quot_msmtch;
    else
        *status = code_succ;
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
                print_words(wlist, fileout);
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
