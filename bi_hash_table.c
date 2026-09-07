#include "bi_hash_table.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

static void* not_null_impl(void* ptr, const char* file, int line, const char* func) {
    if (ptr == NULL) {
        fprintf(stderr,
            "%s:%d: %s: NULL pointer\n",
            file,
            line,
            func);

        abort();
    }

    return ptr;
}

#ifdef NDEBUG
#define NOT_NULL(ptr) (ptr)
#else
#define NOT_NULL(ptr) not_null_impl(ptr, __FILE__, __LINE__, __func__)
#endif

struct bidirectional_hash_table_key_value_pair
{
    void* key;
    void* val;
};

struct bidirectional_hash_table_collision_tree_node
{
    struct bidirectional_hash_table_key_value_pair* key_value_pair;
    struct bidirectional_hash_table_collision_tree_node* left;
    struct bidirectional_hash_table_collision_tree_node* right;
    struct bidirectional_hash_table_collision_tree_node* parent;
    int height;
};

struct bidirectional_hash_table
{
    struct bidirectional_hash_table_collision_tree_node** collision_trees_forward;
    struct bidirectional_hash_table_collision_tree_node** collision_trees_backward;
    size_t capacity;
    size_t size;
    float load_factor_threshold;
    uint64_t(*hash_function_key)  (void*);
    uint64_t (*hash_function_val) (void*);
    int (*compare_function_key)   (void*, void*);
    int (*compare_function_val)   (void*, void*);
};

struct bidirectional_hash_table_key_value_pair_iterator {
    struct bidirectional_hash_table* table;
    size_t table_socket_index;
    struct bidirectional_hash_table_collision_tree_node* current_tree_node;
};

static struct bidirectional_hash_table_collision_tree_node* create_collision_tree_node(struct bidirectional_hash_table_key_value_pair* kv_pair) {
    struct bidirectional_hash_table_collision_tree_node* node = malloc(sizeof(struct bidirectional_hash_table_collision_tree_node));

    NOT_NULL(node);

    node->key_value_pair = kv_pair;
    node->left           = NULL;
    node->right          = NULL;
    node->parent         = NULL;
    node->height         = -1;

    return node;
}

static int height(struct bidirectional_hash_table_collision_tree_node* node) {
    return node == NULL ? -1 : node->height;
}

static float MIN_LOAD_FACTOR_THRESHOLD = 0.1f;

static float fix_load_factor(float load_factor_threshold) {
    if (load_factor_threshold < MIN_LOAD_FACTOR_THRESHOLD) {
        return MIN_LOAD_FACTOR_THRESHOLD;
    }

    return load_factor_threshold;
}

/**************************************************************
Creates a bidirectional hash table with the specified capacity.
**************************************************************/
struct bidirectional_hash_table*
bidirectional_hash_table_create(size_t capacity,
                                float load_factor_threshold,
                                uint64_t (*hash_function_key) (void*),
                                uint64_t (*hash_function_val) (void*),
                                int (*compare_function_key)   (void*, void*),
                                int (*compare_function_val)   (void*, void*)) {

    struct bidirectional_hash_table* table = malloc(sizeof(struct bidirectional_hash_table));

    NOT_NULL(table);

    if (capacity == 0) {
        return NULL;
    }

    // TOOD: from calloc to malloc?
    table->collision_trees_forward  = NOT_NULL(calloc(capacity, sizeof(struct bidirectional_hash_table_collision_tree_node*)));
    table->collision_trees_backward = NOT_NULL(calloc(capacity, sizeof(struct bidirectional_hash_table_collision_tree_node*)));
    table->load_factor_threshold    = fix_load_factor(load_factor_threshold);
    table->hash_function_key        = hash_function_key;
    table->hash_function_val        = hash_function_val;
    table->compare_function_key     = compare_function_key;
    table->compare_function_val     = compare_function_val;
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
    table->hash_function_val        = NULL;
    table->compare_function_key     = NULL;
    table->compare_function_val     = NULL;
}

/*****************************************************************************
Attempts to find a collision tree node by the specified key in the hash table.
Returns a pointer to the node if found, or NULL if not found.
*****************************************************************************/
static struct bidirectional_hash_table_collision_tree_node* get_node_by_key(struct bidirectional_hash_table* table,
                                                                            struct bidirectional_hash_table_collision_tree_node* root, 
                                                                            void* key) {
    if (root == NULL || key == NULL) {
        return NULL;
    }

    struct bidirectional_hash_table_collision_tree_node* node = root;

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
static struct bidirectional_hash_table_collision_tree_node* get_node_by_val(struct bidirectional_hash_table* table,
                                                                            struct bidirectional_hash_table_collision_tree_node* root,
                                                                            void* val) {
    if (table == NULL || val == NULL) {
        return NULL;
    }

    struct bidirectional_hash_table_collision_tree_node* node = root;

    while (node != NULL) {
        int cmp = table->compare_function_val(val, node->key_value_pair->val);

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

/*********************************************************************************
Rotates a tree rooted at 'node1' to the left and returns the new root of the tree.
*********************************************************************************/
static struct bidirectional_hash_table_collision_tree_node* 
tree_rotate_left(struct bidirectional_hash_table_collision_tree_node* node1) {

    struct bidirectional_hash_table_collision_tree_node* node2 = node1->right;

    node2->parent = node1->parent;
    node1->parent = node2;
    node1->right  = node2->left;
    node2->left   = node1;

    if (node1->right != NULL) {
        node1->right->parent = node1;
    }

    node1->height = MAX(height(node1->left), height(node1->right)) + 1;
    node2->height = MAX(height(node2->left), height(node2->right)) + 1;

    return node2;
}

/**********************************************************************************
Rotates a tree rooted at 'node1' to the right and returns the new root of the tree.
**********************************************************************************/
static struct bidirectional_hash_table_collision_tree_node*
tree_rotate_right(struct bidirectional_hash_table_collision_tree_node* node1) {

    struct bidirectional_hash_table_collision_tree_node* node2 = node1->left;

    node2->parent = node1->parent;
    node1->parent = node2;
    node1->left = node2->right;
    node2->right = node1;

    if (node1->left != NULL) {
        node1->left->parent = node1;
    }   

    node1->height = MAX(height(node1->left), height(node1->right)) + 1;    
    node2->height = MAX(height(node2->left), height(node2->right)) + 1;

    return node2;
}

static struct bidirectional_hash_table_collision_tree_node* tree_rotate_right_left(struct bidirectional_hash_table_collision_tree_node* node1) {
    struct bidirectional_hash_table_collision_tree_node* node2 = node1->right;

    node1->right = tree_rotate_right(node2);
    return tree_rotate_left(node1);
}

static struct bidirectional_hash_table_collision_tree_node* tree_rotate_left_right(struct bidirectional_hash_table_collision_tree_node* node1) {
    struct bidirectional_hash_table_collision_tree_node* node2 = node1->left;

    node1->left = tree_rotate_left(node2);   
    return tree_rotate_right(node1);
}

/*****************************************************************************
Balances the AVL tree after an insertion operation to maintain its properties.
*****************************************************************************/
static void fix_after_insertion(
    struct bidirectional_hash_table_collision_tree_node** root,
    struct bidirectional_hash_table_collision_tree_node* node) {

    struct bidirectional_hash_table_collision_tree_node* parent = node->parent;
    struct bidirectional_hash_table_collision_tree_node* grandparent;
    struct bidirectional_hash_table_collision_tree_node* sub_tree;

    while (parent != NULL) {
        if (height(parent->left) == height(parent->right) + 2) {
            grandparent = parent->parent;

            if (height(parent->left->left) >= height(parent->left->right)) {
                sub_tree = tree_rotate_right(parent);
            } else {
                sub_tree = tree_rotate_left_right(parent);
            }

            if (grandparent == NULL) {
                *root = sub_tree;
            } else if (grandparent->left == parent) {
                grandparent->left = sub_tree;
            } else {
                grandparent->right = sub_tree;
            }

            if (grandparent != NULL) {
                grandparent->height = MAX(height(grandparent->left),
                                          height(grandparent->right)) + 1;

            return;
        } else if (height(parent->right) == height(parent->left) + 2) {
            grandparent = parent->parent;

            if (height(parent->right->right) >= height(parent->right->left)) {
                sub_tree = tree_rotate_left(parent);
            } else {
                sub_tree = tree_rotate_right_left(parent);
            }

            }
            if (grandparent == NULL) {
                *root = sub_tree;
            } else if (grandparent->left == parent) {
                grandparent->left = sub_tree;
            } else {
                grandparent->right = sub_tree;
            }

            if (grandparent != NULL) {
                grandparent->height = MAX(height(grandparent->left),
                                          height(grandparent->right)) + 1;
            }

            return;
        }
    }
}

/****************************************************************************
Balances the AVL tree after an deletion operation to maintain its properties.
****************************************************************************/
static void fix_after_deletion(
    struct bidirectional_hash_table_collision_tree_node** root,
    struct bidirectional_hash_table_collision_tree_node* node) {

    struct bidirectional_hash_table_collision_tree_node* parent = node->parent;
    struct bidirectional_hash_table_collision_tree_node* grandparent;
    struct bidirectional_hash_table_collision_tree_node* sub_tree;

    while (parent != NULL) {
        if (height(parent->left) == height(parent->right) + 2) {
            grandparent = parent->parent;

            if (height(parent->left->left) >= height(parent->left->right)) {
                sub_tree = tree_rotate_right(parent);
            } else {
                sub_tree = tree_rotate_left_right(parent);
            }

            if (grandparent == NULL) {
                *root = sub_tree;
            } else if (grandparent->left == parent) {
                grandparent->left = sub_tree;
            } else {
                grandparent->right = sub_tree;
            }

            if (grandparent != NULL) {
                grandparent->height = MAX(height(grandparent->left),
                    height(grandparent->right)) + 1;
            }
        } else if (height(parent->right) == height(parent->left) + 2) {
            grandparent = parent->parent;

            if (height(parent->right->right) >= height(parent->right->left)) {
                sub_tree = tree_rotate_left(parent);
            } else {
                sub_tree = tree_rotate_right_left(parent);
            }

            if (grandparent == NULL) {
                *root = sub_tree;
            } else if (grandparent->left == parent) {
                grandparent->left = sub_tree;
            } else {
                grandparent->right = sub_tree;
            }

            if (grandparent != NULL) {
                grandparent->height = MAX(height(grandparent->left),
                    height(grandparent->right)) + 1;
            }
        }
    }
}

/********************************************************************************
Inserts a key-value pair into the collision tree of the bidirectional hash table.
********************************************************************************/
static void insert_into_collision_tree(struct bidirectional_hash_table_collision_tree_node** root,
                                       struct bidirectional_hash_table_key_value_pair* kv_pair) {

    struct bidirectional_hash_table_collision_tree_node* new_node = create_collision_tree_node(kv_pair);

    if (*root == NULL) {
        *root = new_node;
        return;
    }

    struct bidirectional_hash_table_collision_tree_node* current = *root;
    struct bidirectional_hash_table_collision_tree_node* parent  = NULL;

    while (current != NULL) {
        parent = current;

        if (kv_pair->key < current->key_value_pair->key) {
            current = current->left;
        } else {
            current = current->right;
        }
    }

    new_node->parent = parent;

    if (kv_pair->key < parent->key_value_pair->key) {
        parent->left = new_node;
    } else {
        parent->right = new_node;
    }

    fix_after_insertion(root, new_node);
}

static struct bidirectional_hash_table_collision_tree_node* find_minimum(struct bidirectional_hash_table_collision_tree_node* node) {
    while (node->left != NULL) {
        node = node->left;
    }

    return node;
}

static struct bidirectional_hash_table_collision_tree_node* find_successor(struct bidirectional_hash_table_collision_tree_node* node) {
    if (node->right != NULL) {
        return find_minimum(node->right);
    }

    struct bidirectional_hash_table_collision_tree_node* parent = node->parent;
    
    while (parent != NULL && node == parent->right) {
        node = parent;
        parent = parent->parent;
    }

    return parent;
}

/*************************************************************************************
Deletes a collision tree node from the collision tree of the bidirectional hash table.
*************************************************************************************/
static void delete_from_collision_tree(struct bidirectional_hash_table_collision_tree_node** root,
                                       struct bidirectional_hash_table_collision_tree_node* node) {
    if (node == NULL) {
        return; // TODO: Remove?
    }

    struct bidirectional_hash_table_collision_tree_node* parent = node->parent;
    
    if (node->left == NULL && node->right == NULL) {
        if (parent == NULL) {
            *root = NULL;
        } else if (parent->left == node) {
            parent->left = NULL;
        } else {
            parent->right = NULL;
        }
    } else if (node->left != NULL && node->right != NULL) {
        struct bidirectional_hash_table_collision_tree_node* successor = find_successor(node);

        // Swap the key-value pairs of the node and its successor:
        struct bidirectional_hash_table_key_value_pair* temp_kv_pair = node->key_value_pair;
        node->key_value_pair      = successor->key_value_pair;
        successor->key_value_pair = temp_kv_pair;

        // Recursively delete the successor
        delete_from_collision_tree(root, successor);
    } else {
        struct bidirectional_hash_table_collision_tree_node* child = (node->left != NULL) ? node->left : node->right;

        if (parent == NULL) {
            *root = child;
        } else if (parent->left == node) {
            parent->left = child;
        } else {
            parent->right = child;
        }

        child->parent = parent;
    }
}

static void add_non_existing_key_val_pair(struct bidirectional_hash_table* table, void* key, void* val) {
    struct bidirectional_hash_table_key_value_pair* new_kv_pair = malloc(sizeof(struct bidirectional_hash_table_key_value_pair));

    if (new_kv_pair == NULL) {
        abort();
    }

    new_kv_pair->key = key;
    new_kv_pair->val = val;

    // Insert the new key-value pair into the appropriate collision trees
    const size_t key_index = table->hash_function_key(key) % table->capacity;
    const size_t val_index = table->hash_function_val(val) % table->capacity;

    insert_into_collision_tree(&table->collision_trees_forward [key_index], new_kv_pair);
    insert_into_collision_tree(&table->collision_trees_backward[val_index], new_kv_pair);
}

/************************************************************************************
Inserts a key-value pair into the bidirectional hash table. If the key-value mapping
exists, does nothing. If either the key or the value is already present in the table,
it will be replaced with the new mapping.
************************************************************************************/
bool bidirectional_hash_table_insert(struct bidirectional_hash_table* table, void* key, void* val) {
    if (table == NULL || key == NULL || val == NULL) {
        return false;
    }

    const size_t key_index = table->hash_function_key(key) % table->capacity;
    const size_t val_index = table->hash_function_val(val) % table->capacity;

    struct bidirectional_hash_table_collision_tree_node* existing_key_node = get_node_by_key(table, table->collision_trees_forward [key_index], key);
    struct bidirectional_hash_table_collision_tree_node* existing_val_node = get_node_by_val(table, table->collision_trees_backward[val_index], val);

    if (existing_key_node != NULL && existing_val_node != NULL) {
        if (existing_key_node->key_value_pair == existing_val_node->key_value_pair) {
            // Key-value pair already exists, do nothing.
            return false;
        } else {
            // Key and value are different, remove the existing key-value pair:
            bidirectional_hash_table_remove_by_key(table, existing_key_node->key_value_pair->key);
            bidirectional_hash_table_remove_by_val(table, existing_val_node->key_value_pair->val);

            // Insert new key-value pair:
            bidirectional_hash_table_insert(table, key, val);
            return true;
        }
    } else if (existing_key_node == NULL && existing_val_node == NULL) {
        add_non_existing_key_val_pair(table, key, val);
    } else if (existing_key_node != NULL) {
    
    } else {
        // Insert new key-value pair
    }

    return true;
}

/*********************************************************************************
Finds the value associated with the specified key in the bidirectional hash table.
*********************************************************************************/
void* bidirectional_hash_table_find_by_key(struct bidirectional_hash_table* table, void* key) {
    if (table == NULL || key == NULL) {
        return NULL;
    }

    const size_t key_index = table->hash_function_key(key) % table->capacity;

    struct bidirectional_hash_table_collision_tree_node* node = get_node_by_key(table, table->collision_trees_forward[key_index], key);
    
    if (node != NULL) {
        return node->key_value_pair->val;
    }
   
    return NULL;
}

/*********************************************************************************
Finds the key associated with the specified value in the bidirectional hash table.
*********************************************************************************/
void* bidirectional_hash_table_find_by_value(struct bidirectional_hash_table* table, void* val) {
    if (table == NULL || val == NULL) {
        return NULL;
    }

    const size_t val_index = table->hash_function_val(val) % table->capacity;

    struct bidirectional_hash_table_collision_tree_node* node = get_node_by_val(table, table->collision_trees_backward[val_index], val);
    
    if (node != NULL) {
        return node->key_value_pair->key;
    }
   
    return NULL;
}

/**********************************************************************************************
Removes the key-value pair associated with the specified key from the bidirectional hash table.
**********************************************************************************************/
bool bidirectional_hash_table_remove_by_key(struct bidirectional_hash_table* table, void* key) {
    if (table == NULL || key == NULL) {
        return false;
    }

    const size_t key_index = table->hash_function_key(key) % table->capacity;
    struct bidirectional_hash_table_collision_tree_node* node = get_node_by_key(table, table->collision_trees_forward[key_index], key);
    
    if (node == NULL) {
        return false;
    }

    void* value = node->key_value_pair->val;
    const size_t val_index = table->hash_function_val(value) % table->capacity;

    // Remove the node from both collision trees
    delete_from_collision_tree(&table->collision_trees_forward [key_index], node);
    delete_from_collision_tree(&table->collision_trees_backward[val_index], node);

    // Free the key-value pair
    free(node->key_value_pair);
    free(node);

    table->size--;
    return true;
}

/************************************************************************************************
Removes the key-value pair associated with the specified value from the bidirectional hash table.
************************************************************************************************/
bool bidirectional_hash_table_remove_by_val(struct bidirectional_hash_table* table, void* val) {
    if (table == NULL || val == NULL) {
        return false;
    }

    const size_t val_index = table->hash_function_val(val) % table->capacity;

    struct bidirectional_hash_table_collision_tree_node* node = get_node_by_val(table, table->collision_trees_backward[val_index], val);
    
    if (node == NULL) {
        return false;
    }

    void* key = node->key_value_pair->key;
    const size_t key_index = table->hash_function_key(key) % table->capacity;

    // Remove the node from both collision trees
    delete_from_collision_tree(&table->collision_trees_forward [key_index], node);
    delete_from_collision_tree(&table->collision_trees_backward[val_index], node);

    // Free the key-value pair
    free(node->key_value_pair);
    free(node);

    table->size--;
    return true;
}

/****************************************************************************************
Returns true if the bidirectional hash table contains the specified key, false otherwise.
****************************************************************************************/
bool bidirectional_hash_table_contains_key(struct bidirectional_hash_table* table, void* key) {
    if (table == NULL || key == NULL) {
        return false;
    }

    const size_t key_index = table->hash_function_key(key) % table->capacity;
    struct bidirectional_hash_table_collision_tree_node* node = get_node_by_key(table, table->collision_trees_forward[key_index], key);
    
    return node != NULL;

}

/******************************************************************************************
Returns true if the bidirectional hash table contains the specified value, false otherwise.
******************************************************************************************/
bool bidirectional_hash_table_contains_value(struct bidirectional_hash_table* table, void* val) {
    if (table == NULL || val == NULL) {
        return false;
    }

    const size_t val_index = table->hash_function_val(val) % table->capacity;
    struct bidirectional_hash_table_collision_tree_node* node = get_node_by_val(table, table->collision_trees_backward[val_index], val);
    
    return node != NULL;
}

static size_t find_table_socket_index_starting_from(struct bidirectional_hash_table* table, size_t start_index) {
    for (size_t i = start_index; i != table->capacity; ++i) {
        if (table->collision_trees_forward[i] != NULL) {
            return i;
        }
    }

    return SIZE_MAX;
}

static size_t find_first_table_socket_index(struct bidirectional_hash_table* table) {
    return find_table_socket_index_starting_from(table, 0);
}

/*********************************************************
Creates an iterator over the hash table's key/value pairs.
*********************************************************/
struct bidirectional_hash_table_key_value_pair_iterator* bidirectional_hash_table_create_iterator(struct bidirectional_hash_table* table) {

    struct bidirectional_hash_table_key_value_pair_iterator* it = NOT_NULL(malloc(sizeof *it));

    it->table              = table;
    it->table_socket_index = find_first_table_socket_index(table);
    it->current_tree_node  = find_minimum(table->collision_trees_forward[it->table_socket_index]);

    return it;
}

/*********************************************
Returns true only if there is more to iterate.
*********************************************/
bool bidirectional_hash_table_iterator_has_next(struct bidirectional_hash_table_key_value_pair_iterator* iterator) {
    return iterator->table_socket_index < SIZE_MAX;
}

/**********************************************************************************
Loads the current key/value pair and advances the iteration pointer one pair ahead.
**********************************************************************************/
int bidirectional_hash_table_iterator_next(struct bidirectional_hash_table_key_value_pair_iterator* iterator, void** pkey, void** pval) {
    if (!bidirectional_hash_table_iterator_has_next(iterator)) {
        return -1;
    }

    struct bidirectional_hash_table_collision_tree_node* node = iterator->current_tree_node;

    *pkey = node->key_value_pair->key;
    *pval = node->key_value_pair->val;

    node = find_successor(node);

    if (node == NULL) {
        size_t socket_index = find_table_socket_index_starting_from(iterator->table, 
                                                                    iterator->table_socket_index + 1);

        iterator->table_socket_index = socket_index;
        iterator->current_tree_node  = find_minimum(iterator->table->collision_trees_forward[socket_index]);
    }
}

static void remove_node_from_collision_tree(struct bidirectional_hash_table_collision_tree_node** root, struct bidirectional_hash_table_collision_tree_node* node) {
    /*if (node == NULL) {
        return;
    }
    struct bidirectional_hash_table_collision_tree_node* parent = node->parent;
    if (node->left == NULL && node->right == NULL) {
        if (parent == NULL) {
            *root = NULL;
        } else if (parent->left == node) {
            parent->left = NULL;
        } else {
            parent->right = NULL;
        }
    } else if (node->left != NULL && node->right != NULL) {
        struct bidirectional_hash_table_collision_tree_node* successor = find_successor(node);
        // Swap the key-value pairs of the node and its successor:
        struct bidirectional_hash_table_key_value_pair* temp_kv_pair = node->key_value_pair;
        node->key_value_pair = successor->key_value_pair;
        successor->key_value_pair = temp_kv_pair;
        // Recursively delete the successor
        remove_node_from_collision_tree(root, successor);
    } else {
        struct bidirectional_hash_table_collision_tree_node* child = (node->left != NULL) ? node->left : node->right;
        if (parent == NULL) {
            *root = child;
        } else if (parent->left == node) {
            parent->left = child;
        } else {
            parent->right = child;
        }
        child->parent = parent;
    }
    */
}

/*****************************************************
Removes the most recent key/value pair from the table.
*****************************************************/
int bidirectional_hash_table_iterator_remove(struct bidirectional_hash_table_key_value_pair_iterator* iterator) {
    if (!bidirectional_hash_table_iterator_has_next(iterator)) {
        return -1;
    }

    struct bidirectional_hash_table_collision_tree_node* node = iterator->current_tree_node;

    // Remove the node from the collision tree
    remove_node_from_collision_tree(&iterator->table->collision_trees_forward[iterator->table_socket_index], node);

    // Update the iterator's current tree node
    iterator->current_tree_node = find_successor(node);

    return 0;
}
