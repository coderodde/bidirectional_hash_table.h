#include "bi_hash_table.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct int_array_list {
    int* items;
    size_t size;
};

void int_array_list_init(struct int_array_list* list) {
    list->items = malloc(10 * sizeof(int));
    list->size = 0;
}

void int_array_list_destroy(struct int_array_list* list) {
    free(list->items);
    list->items = NULL;
    list->size = 0;
}   

void int_array_list_append(struct int_array_list* list, int value) {
    list->items[list->size++] = value;
}

int int_array_list_get(struct int_array_list* list, size_t index) {
    return list->items[index];
}

size_t int_array_list_size(struct int_array_list* list) {
    return list->size;
}

uint64_t int_array_list_hash(void* ptr) {
    struct int_array_list* list = ptr;
    uint64_t hash = UINT64_C(14695981039346656037);

    for (size_t i = 0; i < list->size; ++i) {
        uint32_t x = (uint32_t) list->items[i];
        hash ^= x;
        hash *= UINT64_C(1099511628211);
    }

    hash ^= list->size;
    hash *= UINT64_C(1099511628211);
    return hash;
}

int int_array_lists_compare(void* a, void* b) {
    struct int_array_list* list_a = a;
    struct int_array_list* list_b = b;
    int cmp = (int) list_a->size - (int) list_b->size;

    if (cmp != 0) {
        return cmp;
    }

    for (size_t i = 0; i < int_array_list_size(a); ++i) {
        int ia = int_array_list_get(a, i);
        int ib = int_array_list_get(b, i);
        cmp = ia - ib;

        if (cmp != 0) {
            return cmp;
        }
    }

    return 0;
}

int str_cmp(void* a, void* b) {
    const char* str_a = (const char*)a;
    const char* str_b = (const char*)b;
    return strcmp(str_a, str_b);
}

uint64_t hash_c_string(void* ptr) {
    const char* s = (const char*) ptr;
    uint64_t hash = UINT64_C(14695981039346656037);

    for (; *s != '\0'; ++s) {
        hash ^= (unsigned char)*s;       // avoids signed-char issues
        hash *= UINT64_C(1099511628211);
    }

    return hash;
}

int main() {
    struct int_array_list list1;
    struct int_array_list list2;

    int_array_list_init(&list1);
    int_array_list_init(&list2);

    int_array_list_append(&list1, 1);
    int_array_list_append(&list1, 2);

    int_array_list_append(&list2, 1);
    int_array_list_append(&list2, 2);
    int_array_list_append(&list2, 3);

    struct bidirectional_hash_table* table = bidirectional_hash_table_create(
        10, 
        0.75f,
        int_array_list_hash,
        hash_c_string,
        int_array_lists_compare,
        str_cmp);

    bidirectional_hash_table_insert(table, &list1, "First list");
    bidirectional_hash_table_insert(table, &list2, "Second list");

    printf("%d\n", bidirectional_hash_table_contains_key(table, &list1));
    printf("%d\n", bidirectional_hash_table_contains_key(table, &list2));

    int_array_list_append(&list1, 10);
    printf("%d\n", bidirectional_hash_table_contains_key(table, &list1));

    printf("%d\n", bidirectional_hash_table_contains_val(table, "First list"));
    printf("%d\n", bidirectional_hash_table_contains_val(table, "Second list"));
    printf("%d\n", bidirectional_hash_table_contains_val(table, "Third list"));

    struct bidirectional_hash_table_key_value_pair_iterator* iterator = bidirectional_hash_table_create_iterator(table);

    while (bidirectional_hash_table_iterator_has_next(iterator)) {

        void* key;
        void* val;
    
        bidirectional_hash_table_iterator_next(iterator, &key, &val);

        const char* value = val;
        printf("Key: [");

        for (size_t i = 0; i < int_array_list_size(key); ++i) {
            printf("%d", int_array_list_get(key, i));

            if (i < int_array_list_size(key) - 1) {
                printf(", ");
            }
        }

        printf("], Value: %s\n", value);
    }

    bidirectional_hash_table_destroy(table);
    int_array_list_destroy(&list1);
    int_array_list_destroy(&list2);
}