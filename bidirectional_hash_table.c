#include "bi_hash_table.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

static void* not_null(void* ptr) {
    if (ptr == NULL) {
        abort();
    }

    return ptr;
}

struct bidirectional_hash_table_key_value_pair
{
    void* key;
    void* value;
};

struct bidirectional_hash_table_collision_tree_node
{
    struct bidirectional_hash_table_key_value_pair* key_value_pair;
    struct bidirectional_hash_table_collision_tree_node* left;
    struct bidirectional_hash_table_collision_tree_node* right;
    struct bidirectional_hash_table_collision_tree_node* parent;
};

struct bidirectional_hash_table
{
    struct bidirectional_hash_table_collision_tree_node** collision_trees_forward;
    struct bidirectional_hash_table_collision_tree_node** collision_trees_backward;
    size_t capacity;
    size_t size;
    float load_factor_threshold;
    size_t(*hash_function_key)(void*);
    size_t(*hash_function_value)(void*);
    int (*compare_function_key)(void*);
    int (*compare_function_value)(void*);
};

static float MIN_LOAD_FACTOR_THRESHOLD = 0.1f;

static float fix_load_factor(float load_factor_threshold) {
    if (load_factor_threshold < MIN_LOAD_FACTOR_THRESHOLD) {
        return MIN_LOAD_FACTOR_THRESHOLD;
    }

    return load_factor_threshold;
}

/******************************************************************
Initializes a bidirectional hash table with the specified capacity.
******************************************************************/
void bidirectional_hash_table_init(struct bidirectional_hash_table* table,
                                   size_t capacity,
                                   float load_factor_threshold,
                                   size_t(*hash_function_key)(void*),
                                   size_t(*hash_function_value)(void*),
                                   int (*compare_function_key)(void*),
                                   int (*compare_function_value)(void*)) {
    not_null(table);

    if (capacity == 0) {
        abort();
    }

    table->collision_trees_forward  = not_null(calloc(capacity, sizeof(struct bidirectional_hash_table_collision_tree_node*)));
    table->collision_trees_backward = not_null(calloc(capacity, sizeof(struct bidirectional_hash_table_collision_tree_node*)));
    table->load_factor_threshold    = fix_load_factor(load_factor_threshold);
    table->hash_function_key        = hash_function_key;
    table->hash_function_value      = hash_function_value;
    table->compare_function_key     = compare_function_key;
    table->compare_function_value   = compare_function_value;
    table->capacity                 = capacity;
    table->size                     = 0;
}

/************************************************************************
Frees the key-value pairs stored in the collision tree nodes recursively.
Frees also the traversed nodes.
************************************************************************/
static void free_collision_tree_impl_free_kv_mappings_impl(struct bidirectional_hash_table_collision_tree_node* node) {
    if (node == NULL) {
        return;
    }

    free(node->key_value_pair);
    free_collision_tree_impl_free_kv_mappings_impl(node->left);
    free_collision_tree_impl_free_kv_mappings_impl(node->right);
    free(node);
}

/***********************************************
Frees the traversed nodes in the collision tree.
***********************************************/
static void free_collision_tree_impl(struct bidirectional_hash_table_collision_tree_node* node) {
    if (node == NULL) {
        return;
    }

    free_collision_tree_impl(node->left);
    free_collision_tree_impl(node->right);
    free(node);
}

/************************************************************************
Frees the key-value pairs stored in the collision tree nodes recursively.
Frees also the traversed key-value pairs.
************************************************************************/
static void free_collision_tree_free_kv_mappings(struct bidirectional_hash_table_collision_tree_node* node) {
    free_collision_tree_impl_free_kv_mappings_impl(node);
}

/************************************************************************
Frees the key-value pairs stored in the collision tree nodes recursively.
Does not free the traversed key-value pairs.
************************************************************************/
static void free_collision_tree(struct bidirectional_hash_table_collision_tree_node* node) {
    free_collision_tree_impl(node);
}

/*****************************************************************
Destroys a bidirectional hash table and frees all allocated memory.
*****************************************************************/
void bidirectional_hash_table_destroy(struct bidirectional_hash_table* table) {
    if (table == NULL) {
        return;
    }

    for (size_t i = 0; i < table->capacity; ++i) {
        struct bidirectional_hash_table_collision_tree_node* rootf = table->collision_trees_forward[i];
        struct bidirectional_hash_table_collision_tree_node* rootb = table->collision_trees_backward[i];

        free_collision_tree_free_kv_mappings(rootf); // Frees also key-value pairs.
        free_collision_tree(rootb);                  // Does not touch key-value pairs, 
                                                     // as they are already freed by the forward tree.
    }

    free(table->collision_trees_forward);
    free(table->collision_trees_backward);
    
    table->capacity                 = 0;
    table->size                     = 0;
    table->collision_trees_backward = NULL;
    table->collision_trees_forward  = NULL;
    table->hash_function_key        = NULL;
    table->hash_function_value      = NULL;
    table->compare_function_key     = NULL;
    table->compare_function_value   = NULL;
}

/*****************************************************************************
Attempts to find a collision tree node by the specified key in the hash table.
Returns a pointer to the node if found, or NULL if not found.
*****************************************************************************/
static struct bidirectional_hash_table_collision_tree_node* get_node_by_key(struct bidirectional_hash_table* table, void* key) {
    if (table == NULL || key == NULL) {
        return NULL;
    }

    size_t hash = table->hash_function_key(key);
    size_t index = hash % table->capacity;

    struct bidirectional_hash_table_collision_tree_node* node = table->collision_trees_forward[index];

    while (node != NULL) {
        int cmp = table->compare_function_key(key, node->key_value_pair->key);

        if (cmp == 0) {
            return node;
        }

        if (cmp < 0) {
            node = node->left;
        } else {
            node = node->right;
        }
    }

    return NULL;
}

/*****************************************************************************
Attempts to find a collision tree node by the specified key in the hash table.
Returns a pointer to the node if found, or NULL if not found.
*****************************************************************************/
static struct bidirectional_hash_table_collision_tree_node* get_node_by_val(struct bidirectional_hash_table* table, void* value) {
    if (table == NULL || value == NULL) {
        return NULL;
    }

    size_t hash  = table->hash_function_value(value);
    size_t index = hash % table->capacity;

    struct bidirectional_hash_table_collision_tree_node* node = table->collision_trees_backward[index];

    while (node != NULL) {
        int cmp = table->compare_function_value(value, node->key_value_pair->value);

        if (cmp == 0) {
            return node;
        }

        if (cmp < 0) {
            node = node->left;
        } else {
            node = node->right;
        }
    }

    return NULL;
}

/************************************************************************************
Inserts a key-value pair into the bidirectional hash table. If the key-value mapping
exists, does nothing. If either the key or the value is already present in the table,
it will be replaced with the new mapping.
************************************************************************************/
void bidirectional_hash_table_insert(struct bidirectional_hash_table* table, void* key, void* value) {
    if (table == NULL || key == NULL || value == NULL) {
        return;
    }

    struct bidirectional_hash_table_collision_tree_node* existing_key_node   = get_node_by_key(table, key);
    struct bidirectional_hash_table_collision_tree_node* existing_value_node = get_node_by_val(table, value);

    if (existing_key_node != NULL && existing_value_node != NULL) {
        

        // Both key and value already exist, do nothing.
        return;
    }
}

/*********************************************************************************
Finds the value associated with the specified key in the bidirectional hash table.
*********************************************************************************/
void* bidirectional_hash_table_find_by_key(struct bidirectional_hash_table* table, void* key);

/*********************************************************************************
Finds the key associated with the specified value in the bidirectional hash table.
*********************************************************************************/
void* bidirectional_hash_table_find_by_value(struct bidirectional_hash_table* table, void* value);

/**********************************************************************************************
Removes the key-value pair associated with the specified key from the bidirectional hash table.
**********************************************************************************************/
bool bidirectional_hash_table_remove_by_key(struct bidirectional_hash_table* table, void* key);

/************************************************************************************************
Removes the key-value pair associated with the specified value from the bidirectional hash table.
************************************************************************************************/
bool bidirectional_hash_table_remove_by_value(struct bidirectional_hash_table* table, void* value);

/****************************************************************************************
Returns true if the bidirectional hash table contains the specified key, false otherwise.
****************************************************************************************/
bool bidirectional_hash_table_contains_key(struct bidirectional_hash_table* table, void* key);

/******************************************************************************************
Returns true if the bidirectional hash table contains the specified value, false otherwise.
******************************************************************************************/
bool bidirectional_hash_table_contains_value(struct bidirectional_hash_table* table, void* value);
