#include "bi_hash_table.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

uint64_t int_array_list_hash(void* ptr) {
    return 0;
}

int int_array_lists_compare(void* a, void* b) {
    int* ia = *(const int*) a;
    int* ib = *(const int*) b;

    return (ia > ib) - (ia < ib);
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

    srand((unsigned int) time(NULL));

    int**  arrs = calloc(1000, sizeof *arrs);
    char** strs = calloc(1000, sizeof *strs);

    for (size_t i = 0; i < 1000; ++i) {
        arrs[i]    = calloc(1, sizeof(int));
        arrs[i][0] = (int) i + 1;
        strs[i]    = calloc(32, sizeof **strs);

        sprintf_s(strs[i], 32, "string_%zu", i + 1);
    }

    struct bidirectional_hash_table* table = bidirectional_hash_table_create(
        10, 
        0.75f,
        int_array_list_hash,
        hash_c_string,
        int_array_lists_compare,
        str_cmp);

    for (size_t i = 0; i < 1000; ++i) {
        bidirectional_hash_table_insert(table, arrs[i], strs[i]);

        if (!bidirectional_hash_table_check_invariants(table)) {
            printf("Invariants check failed after insertion of key-value pair %zu\n", i);
            return 1;
        }
    }

    for (size_t i = 0; i < 1000; ++i) {
        if (!bidirectional_hash_table_contains_key(table, arrs[i])) {
            printf("Key not found after insertion: %zu\n", i);
            return 1;
        }

        if (!bidirectional_hash_table_contains_val(table, strs[i])) {
            printf("Value not found after insertion: %zu\n", i);
            return 1;
        }
    }

    printf("Table variant OK: %d\n", bidirectional_hash_table_check_invariants(table));

    for (size_t i = 0; i < 500; ++i) {
        if (!bidirectional_hash_table_remove_by_key(table, arrs[i])) {
            printf("Failed to remove key: %zu\n", i);
            return 1;
        }

        if (!bidirectional_hash_table_check_invariants(table)) {
            printf("Invariants check failed after removal of key-value pair %zu\n", i);
            return 1;
        }
    }

    struct bidirectional_hash_table_key_value_pair_iterator* it = bidirectional_hash_table_create_iterator(table);

    while (bidirectional_hash_table_iterator_has_next(it)) {
        void* key;
        void* val;

        if (!bidirectional_hash_table_iterator_next(it, &key, &val)) {
            printf("Iterator failed to get next key-value pair\n");
            return 1;
        }

        if (!bidirectional_hash_table_contains_key(table, key)) {
            printf("Iterator returned a key not in the table\n");
            return 1;
        }

        if (!bidirectional_hash_table_contains_val(table, val)) {
            printf("Iterator returned a value not in the table\n");
            return 1;
        }

        bidirectional_hash_table_iterator_remove(it);
    }

    if (!bidirectional_hash_table_is_empty(table)) {
        printf("Table is NOT empty after iterator removals.\n");
        return 1;
    }

    bidirectional_hash_table_destroy(table);

    for (size_t i = 0; i < 1000; ++i) {
        free(arrs[i]);
        free(strs[i]);
    }

    free(arrs);
    free(strs);

    puts("[STATUS] All tests passed.");

    return 0;
}