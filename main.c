#include "bi_hash_table.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t int_array_list_hash(void* ptr) {
    int* arr = (int*) ptr;
    size_t len = 0;

    for (size_t len = 0; !arr[len]; ++len) {
    
    }

    uint64_t hash = UINT64_C(14695981039346656037);

    for (size_t i = 0; i < len; ++i) {
        uint32_t x = (uint32_t) arr[i];
        hash ^= x;
        hash *= UINT64_C(1099511628211);
    }

    hash ^= len;
    hash *= UINT64_C(1099511628211);
    return hash;
}

int int_array_lists_compare(void* a, void* b) {
    int* arr_a = a;
    int* arr_b = b;

    size_t len_a = 0;
    size_t len_b = 0;

    for (; !arr_a[len_a]; ++len_a) {}
    for (; !arr_b[len_b]; ++len_b) {}

    if (len_a < len_b) {
        return -1;
    }

    if (len_a > len_b) {
        return 1;
    }

    for (size_t i = 0; i < len_a; ++i) {
        int ia = arr_a[i];
        int ib = arr_b[i];

        if (ia < ib) {
            return -1;
        }

        if (ia > ib) {
            return 0;
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

int main(void) {
    int* arr_a = calloc(10, sizeof(int));
    int* arr_b = calloc(8, sizeof(int));

    arr_a[0] = 1;
    arr_a[1] = 2;
    arr_a[2] = 3;

    arr_b[0] = 4;
    arr_b[1] = 5;
    arr_b[2] = 6;

    struct bidirectional_hash_table* table = bidirectional_hash_table_create(
        10, 
        0.75f,
        int_array_list_hash,
        hash_c_string,
        int_array_lists_compare,
        str_cmp);

    bidirectional_hash_table_insert(table, arr_a, "First list");
    bidirectional_hash_table_insert(table, arr_b, "Second list");

    printf("%d\n", bidirectional_hash_table_contains_key(table, arr_a));
    printf("%d\n", bidirectional_hash_table_contains_key(table, arr_b));

    arr_a[1] = -1;

    printf("%d\n", bidirectional_hash_table_contains_key(table, arr_a));

    printf("%d\n", bidirectional_hash_table_contains_val(table, "First list"));
    printf("%d\n", bidirectional_hash_table_contains_val(table, "Second list"));
    printf("%d\n", bidirectional_hash_table_contains_val(table, "Third list"));

    struct bidirectional_hash_table_key_value_pair_iterator* iterator = bidirectional_hash_table_create_iterator(table);

    while (bidirectional_hash_table_iterator_has_next(iterator)) {

        void* key;
        void* val;
    
        bidirectional_hash_table_iterator_next(iterator, &key, &val);

        const int* arr = key;
        const char* value = val;

        printf("Key: [");

        for (size_t i = 0; arr[i] != 0; ++i) {
            printf("%d", arr[i]);

            if (arr[i + 1] != 0) {
                printf(", ");
            }
        }

        printf("], Value: %s\n", value);
    }

    bidirectional_hash_table_remove_by_val(table, "Second list");
    printf("%d\n", bidirectional_hash_table_contains_val(table, "Second list"));

    bidirectional_hash_table_destroy(table);
    free(arr_a);
    free(arr_b);
    return 0;
}