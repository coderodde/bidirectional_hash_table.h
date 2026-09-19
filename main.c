#include "bi_hash_table.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

uint64_t int_array_list_hash(void* ptr) {
    return 3;
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

    srand((unsigned int)time(NULL));

    int** arrs  = calloc(1000, sizeof(int*));
    char** strs = calloc(1000, sizeof(char*));

    for (size_t i = 0; i < 1000; ++i) {
        arrs[i] = calloc(10, sizeof(int));

        for (size_t j = 0; j < 10; ++j) {
            arrs[i][j] = (int) rand() % 100;
        }

        arrs[i][rand() % 10] = 0;
    }

    for (size_t i = 0; i < 1000; ++i) {
        strs[i] = calloc(10, sizeof(char));

        for (size_t j = 0; j < 10; ++j) {
            strs[i][j] = (char)(rand() % 26 + 'a');
        }

        strs[i][rand() % 10] = '\0';
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

    for (size_t i = 0; i < 1000; ++i) {
        if (!bidirectional_hash_table_remove_by_key(table, arrs[i])) {
            printf("Failed to remove key: %zu\n", i);
            return 1;
        }

        if (!bidirectional_hash_table_check_invariants(table)) {
            printf("Invariants check failed after removal of key-value pair %zu\n", i);
            return 1;
        }
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