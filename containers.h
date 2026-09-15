#ifndef _H_CONTAINERS_SENTRY_
#define _H_CONTAINERS_SENTRY_
# include <stdlib.h>

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

void l_append(struct l_list *list, char val);
void l_delete(struct l_list *list);
void l_backspace(struct l_list *list);
void l_list_print(struct l_list *list);
char *l_list_to_str(struct l_list *list);
struct l_list *str_to_l_list(const char *str);
void l_list_init(struct l_list *list);
void l_list_free(struct l_list *list);

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

/* structure for dynamic strings (don't forget to append '\0') */
struct d_str {
    int size;
    int capacity;
    char *items;
};

/* structure for array of strings */
struct str_arr {
    int size;
    int capacity;
    char **items;
};

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

#define DA_SET_TO_ZERO(da) do {\
    (da)->size = 0;\
    (da)->capacity = 0;\
    (da)->items = NULL;\
} while(0)

void str_arr_free(struct str_arr *strs);
void str_arr_sort(struct str_arr *strs, int asc);

#endif /* _H_CONTAINERS_SENTRY_ */
