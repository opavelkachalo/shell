#include <stdio.h>
#include <stdlib.h>
#include "containers.h"

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

void l_append(struct l_list *list, char val)
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

void l_delete(struct l_list *list)
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

void l_backspace(struct l_list *list)
{
    struct l_item *tmp = list->curp;
    if(!tmp)
        return;
    list->curp = tmp->prev;
    list->curpos--;
    l_delete(list);
}

void l_list_print(struct l_list *list)
{
    struct l_item *tmp;
    for(tmp = list->head; tmp; tmp = tmp->next)
        putchar(tmp->val);
}

char *l_list_to_str(struct l_list *list)
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

void l_list_init(struct l_list *list)
{
    list->head = NULL;
    list->tail = NULL;
    list->curp = NULL;
    list->size = 0;
    list->curpos = 1;
}

void l_list_free(struct l_list *list)
{
    struct l_item *tmp;
    while(list->head) {
        tmp = list->head;
        list->head = list->head->next;
        free(tmp);
    }
}
