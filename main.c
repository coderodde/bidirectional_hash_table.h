#include "bi_hash_table.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static size_t passed_assertions = 0;
static size_t failed_assertions = 0;

static void REPORT() {
    printf("Passed assertions: %zu\n", passed_assertions);
    printf("Failed assertions: %zu\n", failed_assertions);
    printf("Total assertions:  %zu\n", passed_assertions + failed_assertions);
    printf("Success rate:     %.2f%%\n", (double)passed_assertions / (passed_assertions + failed_assertions) * 100.0);

    if (failed_assertions > 0) {
        exit(EXIT_FAILURE);
    } else {
        puts("[STATUS] All tests passed.");
    }
}

static void ASSERT(bool condition, const char* message) {
    if (condition) {
        passed_assertions++;
    } else {
        failed_assertions++;
        fprintf(stderr, "Assertion failed: %s\n", message);
    }
}

size_t int_ptr_hash(void* ptr) {
    return (size_t)(uintptr_t) ptr % 30;
}

int int_ptr_compare(void* a, void* b) {
    const uintptr_t ia = (uintptr_t) a;
    const uintptr_t ib = (uintptr_t) b;

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

#if defined _WIN32
        sprintf_s(strs[i], 32, "string_%zu", i + 1);
#else
        sprintf(strs[i], "string_%zu", i + 1); 
#endif
    }

    struct bidirectional_hash_table* table = bidirectional_hash_table_create(
        10, 
        0.75f,
        int_ptr_hash,
        hash_c_string,
        int_ptr_compare,
        str_cmp);

    for (size_t i = 0; i < 1000; ++i) {
        bidirectional_hash_table_insert(table, arrs[i], strs[i]);
        ASSERT(bidirectional_hash_table_contains_key(table, arrs[i]), "Key not found after insertion.");
        ASSERT(bidirectional_hash_table_contains_val(table, strs[i]), "Value not found after insertion.");
        ASSERT(bidirectional_hash_table_find_by_key(table, arrs[i]) == strs[i], "Find by key returned incorrect value.");
        ASSERT(bidirectional_hash_table_find_by_val(table, strs[i]) == arrs[i], "Find by value returned incorrect key.");
        ASSERT(bidirectional_hash_table_check_invariants(table), "Invariants check failed after insertion.");
    }

    for (size_t i = 0; i < 1000; ++i) {
        if (!bidirectional_hash_table_contains_key(table, arrs[i])) {
            ASSERT(bidirectional_hash_table_contains_key(table, arrs[i]), "Key not found after insertion.");
        }

        if (!bidirectional_hash_table_contains_val(table, strs[i])) {
            ASSERT(bidirectional_hash_table_contains_val(table, strs[i]), "Value not found after insertion.");
        }
    }

    for (size_t i = 0; i < 500; ++i) {
        ASSERT(bidirectional_hash_table_remove_by_key(table, arrs[i]), "Failed to remove key-value pair.");
        ASSERT(bidirectional_hash_table_check_invariants(table), "Invariants check failed after removal of key-value pair.");
    }

    struct bidirectional_hash_table_key_value_pair_iterator* it = bidirectional_hash_table_create_iterator(table);

    while (bidirectional_hash_table_iterator_has_next(it)) {
        void* key;
        void* val;

        ASSERT(bidirectional_hash_table_iterator_next(it, &key, &val), "Iterator failed to get next key-value pair");
        ASSERT(bidirectional_hash_table_contains_key(table, key), "Iterator returned a key not in the table.");
        ASSERT(bidirectional_hash_table_contains_val(table, val), "Iterator returned a value not in the table.");
        ASSERT(bidirectional_hash_table_iterator_remove(it), "Failed to remove key-value pair via iterator.");
    }

    ASSERT(bidirectional_hash_table_is_empty(table), "Table is not empty after iterator removals.");

    ASSERT(bidirectional_hash_table_insert(table, (void*) 2, "two"), "Failed to insert key-value pair.");
    
    ASSERT(bidirectional_hash_table_contains_key(table, (void*) 2), "Key not found after insertion.");
    ASSERT(bidirectional_hash_table_contains_val(table, "two"), "Value not found after insertion.");
    ASSERT(bidirectional_hash_table_find_by_key(table, (void*)2) == "two", "Find by key returned incorrect value.");
    ASSERT(bidirectional_hash_table_find_by_val(table, "two") == (void*)2, "Find by value returned incorrect key.");
    
    ASSERT(bidirectional_hash_table_insert(table, (void*)2, "three"), "Failed to update value for existing key.");

    ASSERT(bidirectional_hash_table_contains_key(table, (void*)2), "Key not found after updating value.");
    ASSERT(bidirectional_hash_table_contains_val(table, "three"), "Updated value not found after insertion.");
    ASSERT(bidirectional_hash_table_find_by_key(table, (void*)2) == "three", "Find by key returned incorrect updated value.");
    ASSERT(bidirectional_hash_table_find_by_val(table, "three") == (void*)2, "Find by value returned incorrect key for updated value.");
    
    ASSERT(bidirectional_hash_table_insert(table, (void*) 3, "three"), "Failed to insert key-value pair.");

    ASSERT(bidirectional_hash_table_contains_key(table, (void*)3), "Key not found after insertion.");
    ASSERT(bidirectional_hash_table_contains_val(table, "three"), "Value not found after insertion.");
    ASSERT(bidirectional_hash_table_find_by_key(table, (void*)3) == "three", "Find by key returned incorrect value.");
    ASSERT(bidirectional_hash_table_find_by_val(table, "three") == (void*)3, "Find by value returned incorrect key.");

    bidirectional_hash_table_iterator_destroy(it);

    table = bidirectional_hash_table_create(
        10,
        0.75f,
        int_ptr_hash,
        int_ptr_hash,
        int_ptr_compare,
        int_ptr_compare);

    ASSERT(bidirectional_hash_table_insert(table, (void*)1, (void*)1), "Failed to insert key-value pair during iteration.");
    ASSERT(bidirectional_hash_table_insert(table, (void*)2, (void*)2), "Failed to insert key-value pair during iteration.");
    ASSERT(bidirectional_hash_table_insert(table, (void*)3, (void*)3), "Failed to insert key-value pair during iteration.");
    ASSERT(bidirectional_hash_table_insert(table, (void*)4, (void*)4), "Failed to insert key-value pair during iteration.");

    it = bidirectional_hash_table_create_iterator(table);

    while (bidirectional_hash_table_iterator_has_next(it)) {
        void* key;
        void* val;
        ASSERT(bidirectional_hash_table_iterator_next(it, &key, &val), "Iterator failed to get next key-value pair during iteration.");
        ASSERT(bidirectional_hash_table_contains_key(table, key), "Iterator returned a key not in the table during iteration.");
        ASSERT(bidirectional_hash_table_contains_val(table, val), "Iterator returned a value not in the table during iteration.");

        if (key == (void*)2 || key == (void*)1 || val == (void*)4) {
            ASSERT(bidirectional_hash_table_iterator_remove(it), "Failed to remove key-value pair via iterator during iteration.");
        }
    }

    bidirectional_hash_table_iterator_destroy(it);
    it = bidirectional_hash_table_create_iterator(table);

    ASSERT(bidirectional_hash_table_iterator_has_next(it), "Iterator should have next key-value pair.");
    
    void* key;
    void* val;

    ASSERT(bidirectional_hash_table_iterator_next(it, &key, &val), "Iterator failed to get next key-value pair.");

    ASSERT(key == (void*)3, "Iterator returned incorrect key after removals.");
    ASSERT(val == (void*)3, "Iterator returned incorrect value after removals.");
    ASSERT(bidirectional_hash_table_iterator_remove(it), "Failed to remove key-value pair via iterator after removals.");
    ASSERT(!bidirectional_hash_table_iterator_remove(it), "Iterator should not be able to remove again without calling next.");
    ASSERT(!bidirectional_hash_table_iterator_next(it, &key, &val), "Iterator should not have next key-value pair after removals.");
    ASSERT(bidirectional_hash_table_is_empty(table), "Table should be empty after all removals.");

    bidirectional_hash_table_iterator_destroy(it);
    bidirectional_hash_table_destroy(table);

    for (size_t i = 0; i < 1000; ++i) {
        free(arrs[i]);
        free(strs[i]);
    }

    free(arrs);
    free(strs);

    REPORT();

    return EXIT_SUCCESS;
}
