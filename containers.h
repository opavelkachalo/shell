#define _H_CONTAINERS_SENTRY_
#ifdef _H_CONTAINERS_SENTRY_

struct l_item {
    struct l_item *next;
    struct l_item *prev;
    char val;
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
};

extern void l_append(struct l_list *list, char val);
extern void l_delete(struct l_list *list);
extern void l_backspace(struct l_list *list);
extern void l_list_print(struct l_list *list);
extern char *l_list_to_str(struct l_list *list);
extern void l_list_init(struct l_list *list);
extern void l_list_free(struct l_list *list);

/*
  "Generic" Dynamic Arrays.
  to use them, create a structure with following fields:
    int size;
    int capacity;
    [type] *items;
  where [type] is the type of array elements.
  !! structure *must* be zero-initialized before using it
  !! pass a pointer to the structure when using macros
 */
#define DA_APPEND(da, elem) do {\
    if((da)->size == (da)->capacity) {\
        if((da)->capacity == 0)\
            (da)->capacity = 1;\
        else\
            (da)->capacity *= 2;\
        (da)->items = realloc((da)->items,\
                              sizeof(*(da)->items) * (da)->capacity);\
    }\
    (da)->items[(da)->size] = (elem);\
    (da)->size++;\
} while(0)

#endif /* _H_CONTAINERS_SENTRY_ */
