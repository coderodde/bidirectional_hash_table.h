#include "bi_hash_table.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MINIMUM_CAPACITY 8

enum direction {
    FORWARD,
    BACKWARD
};

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
    size_t load_capacity;
    float load_factor_threshold;
    size_t (*hash_function_key) (void*);
    size_t (*hash_function_val) (void*);
    int (*compare_function_key) (void*, void*);
    int (*compare_function_val) (void*, void*);
};

struct bidirectional_hash_table_key_value_pair_iterator {
    struct bidirectional_hash_table* table;
    struct bidirectional_hash_table_collision_tree_node* current_tree_node;
    struct bidirectional_hash_table_collision_tree_node* next_tree_node;
    size_t table_socket_index;
};

static bool need_to_enlarge_table(struct bidirectional_hash_table* table) {
    return (float) table->size / (float) table->capacity >= table->load_factor_threshold;
}

static bool need_to_shrink_table(struct bidirectional_hash_table* table) {
    if (table->capacity <= MINIMUM_CAPACITY) {
        return false;
    }

    return (float) table->size / (float) table->capacity < table->load_factor_threshold / 4.0f;
}

static struct bidirectional_hash_table_collision_tree_node* create_collision_tree_node(struct bidirectional_hash_table_key_value_pair* kv_pair) {
    struct bidirectional_hash_table_collision_tree_node* node = NOT_NULL(malloc(sizeof(struct bidirectional_hash_table_collision_tree_node)));

    node->key_value_pair = kv_pair;
    node->left           = NULL;
    node->right          = NULL;
    node->parent         = NULL;
    node->height         = 0;

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
static void fix_after_insertion(
    struct bidirectional_hash_table_collision_tree_node** root,
    struct bidirectional_hash_table_collision_tree_node* node);

/************************************************************************************************
Inserts a key-value pair and a tree node into the collision tree of the bidirectional hash table.
************************************************************************************************/
static bool insert_into_collision_tree(struct bidirectional_hash_table* table,
    						           struct bidirectional_hash_table_collision_tree_node** root,
                                       struct bidirectional_hash_table_collision_tree_node* node,
                                       struct bidirectional_hash_table_key_value_pair* kv_pair,
                                       enum direction dir) {
    if (node == NULL) {
        return false;
    }

    if (*root == NULL) {
        *root = node;
        return true;
    }

    struct bidirectional_hash_table_collision_tree_node* current = *root;
    struct bidirectional_hash_table_collision_tree_node* parent  = NULL;

    while (current != NULL) {
        parent = current;

        int cmp;

        if (dir == FORWARD) {
            cmp = table->compare_function_key(kv_pair->key, current->key_value_pair->key);
        } else {
            cmp = table->compare_function_val(kv_pair->val, current->key_value_pair->val);
        }

        if (cmp < 0) {
            current = current->left;
        } else {
            current = current->right;
        }
    }

    node->parent = parent;

    int cmp;

    if (dir == FORWARD) {
        cmp = table->compare_function_key(kv_pair->key, parent->key_value_pair->key);
    } else {
        cmp = table->compare_function_val(kv_pair->val, parent->key_value_pair->val);
    }

    if (cmp < 0) {
        parent->left = node;
    } else {
        parent->right = node;
    }

    fix_after_insertion(root, node);
    return true;
}

/************************************************
Rehashes the 'node' to the 'new_collision_trees'.
************************************************/
static void rehash_collision_tree(struct bidirectional_hash_table* table,
                                  struct bidirectional_hash_table_collision_tree_node* node,
                                  struct bidirectional_hash_table_collision_tree_node** new_collision_trees,
                                  size_t new_capacity,
                                  size_t(*hash_function) (void*),
                                  enum direction dir) {
    if (node == NULL) {
        return;
    }

    struct bidirectional_hash_table_collision_tree_node* left_child  = node->left;
    struct bidirectional_hash_table_collision_tree_node* right_child = node->right;

    node->left   = NULL;
    node->right  = NULL;
    node->parent = NULL;
    node->height = 0;


    rehash_collision_tree(table, 
                          left_child,  
                          new_collision_trees, 
                          new_capacity, 
                          hash_function, 
                          dir);
                          
    rehash_collision_tree(table, 
                          right_child,
                          new_collision_trees, 
                          new_capacity, 
                          hash_function, 
                          dir);

    void* object = dir == FORWARD 
                 ? node->key_value_pair->key 
                 : node->key_value_pair->val;
                 
    size_t index = hash_function(object) % new_capacity;

    insert_into_collision_tree(table, 
                               &new_collision_trees[index], 
                               node, 
                               node->key_value_pair, 
                               dir);
}

static void rehash_table(struct bidirectional_hash_table* table,
                         size_t new_capacity,
                         size_t(*hash_function) (void*),
                         enum direction dir) {

    struct bidirectional_hash_table_collision_tree_node** old_collision_trees = 
        (dir == FORWARD) 
        ? table->collision_trees_forward
        : table->collision_trees_backward;
        
    struct bidirectional_hash_table_collision_tree_node** new_collision_trees = 
        NOT_NULL(calloc(new_capacity, 
                        sizeof(struct bidirectional_hash_table_collision_tree_node*)));
                        
    const size_t old_capacity = table->capacity;

    for (size_t i = 0; i < old_capacity; ++i) {
        struct bidirectional_hash_table_collision_tree_node* root = old_collision_trees[i];

        if (root != NULL) {
            rehash_collision_tree(table, 
                                  root, 
                                  new_collision_trees, 
                                  new_capacity, 
                                  hash_function, 
                                  dir);        
        }
    }

    free(old_collision_trees);

    if (dir == FORWARD) {
        table->collision_trees_forward  = new_collision_trees;
    } else {
        table->collision_trees_backward = new_collision_trees;
    }
}

/**************************************************************
Creates a bidirectional hash table with the specified capacity.
**************************************************************/
struct bidirectional_hash_table*
bidirectional_hash_table_create(size_t capacity,
                                float load_factor_threshold,
                                size_t (*hash_function_key) (void*),
                                size_t (*hash_function_val) (void*),
                                int (*compare_function_key) (void*, void*),
                                int (*compare_function_val) (void*, void*)) {

    if (capacity == 0) {
        return NULL;
    }

    load_factor_threshold = fix_load_factor(load_factor_threshold);
    size_t load_capacity  = (size_t)(capacity * load_factor_threshold);

    struct bidirectional_hash_table* table = NOT_NULL(malloc(sizeof(struct bidirectional_hash_table)));

    // TOOD: from calloc to malloc?
    table->collision_trees_forward  = NOT_NULL(calloc(capacity, sizeof(struct bidirectional_hash_table_collision_tree_node*)));
    table->collision_trees_backward = NOT_NULL(calloc(capacity, sizeof(struct bidirectional_hash_table_collision_tree_node*)));
    table->load_factor_threshold    = load_factor_threshold;
    table->hash_function_key        = hash_function_key;
    table->hash_function_val        = hash_function_val;
    table->compare_function_key     = compare_function_key;
    table->compare_function_val     = compare_function_val;
    table->capacity                 = capacity;
    table->size                     = 0;
    table->load_capacity            = load_capacity;

    return table;
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

    free(table);
}

/*****************************************************************************
Attempts to find a collision tree node by the specified key in the hash table.
Returns a pointer to the node if found, or NULL if not found.
*****************************************************************************/
static struct bidirectional_hash_table_collision_tree_node* get_node_by_key(
    struct bidirectional_hash_table* table,
    struct bidirectional_hash_table_collision_tree_node* root, 
    void* key) {
    
    if (root == NULL) {
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
static struct bidirectional_hash_table_collision_tree_node* get_node_by_val(
    struct bidirectional_hash_table* table,
    struct bidirectional_hash_table_collision_tree_node* root,
    void* val) {
    
    if (table == NULL) {
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

static struct bidirectional_hash_table_collision_tree_node* tree_rotate_left_right(struct bidirectional_hash_table_collision_tree_node* node) {
    node->left = tree_rotate_left(node->left);
    return tree_rotate_right(node);
}

static struct bidirectional_hash_table_collision_tree_node* tree_rotate_right_left(struct bidirectional_hash_table_collision_tree_node* node) {
    node->right = tree_rotate_right(node->right);
    return tree_rotate_left(node);
}

/*****************************************************************************
Balances the AVL tree after an insertion operation to maintain its properties.
*****************************************************************************/
static void fix_after_insertion(
    struct bidirectional_hash_table_collision_tree_node** root,
    struct bidirectional_hash_table_collision_tree_node* node) {

    struct bidirectional_hash_table_collision_tree_node* parent = node->parent;

    while (parent != NULL) {
        parent->height = MAX(height(parent->left), height(parent->right)) + 1;
        
        int balance = height(parent->left) - height(parent->right);
        
        if (balance == 2) {
            struct bidirectional_hash_table_collision_tree_node* grandparent = parent->parent;
            struct bidirectional_hash_table_collision_tree_node* sub_tree;

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

            return;
        }

        if (balance == -2) {
            struct bidirectional_hash_table_collision_tree_node* grandparent = parent->parent;
            struct bidirectional_hash_table_collision_tree_node* sub_tree;

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

            return;
        }

        parent = parent->parent;
    }
}

/*****************************************************************************
Balances the AVL tree after an deletion operation to maintain its properties.
*****************************************************************************/
static void fix_after_deletion(
    struct bidirectional_hash_table_collision_tree_node** root,
    struct bidirectional_hash_table_collision_tree_node* node) {

    while (node != NULL) {
        node->height = MAX(height(node->left), height(node->right)) + 1;

        struct bidirectional_hash_table_collision_tree_node* parent   = node->parent;
        struct bidirectional_hash_table_collision_tree_node* sub_tree = node;

        if (height(node->left) == height(node->right) + 2) {
            if (height(node->left->left) >= height(node->left->right)) {
                sub_tree = tree_rotate_right(node);
            } else {
                sub_tree = tree_rotate_left_right(node);
            }
        } else if (height(node->right) == height(node->left) + 2) {
            if (height(node->right->right) >= height(node->right->left)) {
                sub_tree = tree_rotate_left(node);
            } else {
                sub_tree = tree_rotate_right_left(node);
            }
        }

        if (sub_tree != node) {

            if (parent == NULL) {
                *root = sub_tree;
            } else if (parent->left == node) {
                parent->left = sub_tree;
            } else {
                parent->right = sub_tree;
            }

            node = parent;
        } else {
            node = node->parent;
        }
    }
}

/********************************************************
Finds the minimum tree node in the tree rooted at 'node'.
********************************************************/
static struct bidirectional_hash_table_collision_tree_node* find_minimum(struct bidirectional_hash_table_collision_tree_node* node) {
    while (node->left != NULL) {
        node = node->left;
    }

    return node;
}

/**********************************
Finds the successor node of 'node'.
**********************************/
static struct bidirectional_hash_table_collision_tree_node* find_successor(struct bidirectional_hash_table_collision_tree_node* node) {
    if (node->right != NULL) {
        return find_minimum(node->right);
    }

    struct bidirectional_hash_table_collision_tree_node* parent = node->parent;
    
    while (parent != NULL && node == parent->right) {
        node   = parent;
        parent = parent->parent;
    }

    return parent;
}

/*************************************************************************************
Deletes a collision tree node from the collision tree of the bidirectional hash table.
*************************************************************************************/
static struct bidirectional_hash_table_collision_tree_node* 
delete_from_collision_tree(struct bidirectional_hash_table_collision_tree_node** root,
                           struct bidirectional_hash_table_collision_tree_node*  node) {
    
    if (node->left != NULL && node->right != NULL) {
        struct bidirectional_hash_table_collision_tree_node* successor_node = find_minimum(node->right);
        struct bidirectional_hash_table_key_value_pair* tmp = node->key_value_pair;

        node->key_value_pair = successor_node->key_value_pair;
        successor_node->key_value_pair = tmp;
        node = successor_node;
    }

    struct bidirectional_hash_table_collision_tree_node* child  = node->left != NULL ? node->left : node->right;
    struct bidirectional_hash_table_collision_tree_node* parent = node->parent;

    if (child != NULL) {
        child->parent = parent;
    }

    if (parent == NULL) {
        *root = child;
    } else if (parent->left == node) {
        parent->left = child;
    } else {
        parent->right = child;
    }

    fix_after_deletion(root, parent);

    node->left = NULL;
    node->right = NULL;
    node->parent = NULL;

    return node;
}

static bool add_non_existing_key_val_pair(struct bidirectional_hash_table* table, void* key, void* val) {
    struct bidirectional_hash_table_key_value_pair* new_kv_pair = malloc(sizeof(struct bidirectional_hash_table_key_value_pair));

    if (new_kv_pair == NULL) {
        return false;
    }

    new_kv_pair->key = key;
    new_kv_pair->val = val;

    // Insert the new key-value pair into the appropriate collision trees
    const size_t key_index = table->hash_function_key(key) % table->capacity;
    const size_t val_index = table->hash_function_val(val) % table->capacity;

    struct bidirectional_hash_table_collision_tree_node* new_node_forward  = create_collision_tree_node(new_kv_pair);
    struct bidirectional_hash_table_collision_tree_node* new_node_backward = create_collision_tree_node(new_kv_pair);

    if (!insert_into_collision_tree(table, &table->collision_trees_forward[key_index], new_node_forward, new_kv_pair, FORWARD)) {
        free(new_kv_pair);
        return false;
    }

    if (!insert_into_collision_tree(table, &table->collision_trees_backward[val_index], new_node_backward, new_kv_pair, BACKWARD)) {
        // Rollback the insertion into the forward tree if the backward insertion fails
        struct bidirectional_hash_table_collision_tree_node* node_to_remove = get_node_by_key(table, table->collision_trees_forward[key_index], key);
        struct bidirectional_hash_table_collision_tree_node* removed_node   = delete_from_collision_tree(&table->collision_trees_forward[key_index], node_to_remove);
        
        free(removed_node);
        free(new_kv_pair);
        return false;
    }

    table->size++;
    return true;
}

/************************************************************************************
Inserts a key-value pair into the bidirectional hash table. If the key-value mapping
exists, does nothing. If either the key or the value is already present in the table,
it will be replaced with the new mapping.
************************************************************************************/
bool bidirectional_hash_table_insert(struct bidirectional_hash_table* table, void* key, void* val) {
    if (table == NULL) {
        return false;
    }

    if (need_to_enlarge_table(table)) {
        size_t new_capacity = table->capacity * 2;
        
        rehash_table(table, new_capacity, table->hash_function_key, FORWARD);
        rehash_table(table, new_capacity, table->hash_function_val, BACKWARD);

        table->capacity      = new_capacity;
        table->load_capacity = (size_t)(new_capacity * table->load_factor_threshold);
    }

    const size_t key_index = table->hash_function_key(key) % table->capacity;
          size_t val_index = table->hash_function_val(val) % table->capacity;

    struct bidirectional_hash_table_collision_tree_node* existing_key_node = get_node_by_key(table, table->collision_trees_forward [key_index], key);
    struct bidirectional_hash_table_collision_tree_node* existing_val_node = get_node_by_val(table, table->collision_trees_backward[val_index], val);

    if (existing_key_node != NULL &&
        existing_val_node != NULL &&
        existing_key_node->key_value_pair == 
        existing_val_node->key_value_pair) {
        return false; // Already present mapping, no changes.
    }

    if (existing_key_node != NULL) {
        if (!bidirectional_hash_table_remove_by_key(table, key)) {
            return false;
        }
    }

    // We might have 
    val_index = table->hash_function_val(val) % table->capacity;

    existing_val_node = get_node_by_val(table, table->collision_trees_backward[val_index], val);

    if (existing_val_node != NULL) {
        if (!bidirectional_hash_table_remove_by_val(table, val)) {
            return false;
        }
    }

    return add_non_existing_key_val_pair(table, key, val);
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
void* bidirectional_hash_table_find_by_val(struct bidirectional_hash_table* table, void* val) {
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

/********************************************************************************
The actual implementation of the bidirectional_hash_table_remove_by_key function.
********************************************************************************/
static bool bidirectional_hash_table_remove_by_key_impl(struct bidirectional_hash_table* table, void* key, bool allow_shrink) {
    if (table == NULL) {
        return false;
    }

    if (need_to_shrink_table(table) && allow_shrink) {
        size_t new_capacity = table->capacity / 2;

        rehash_table(table, new_capacity, table->hash_function_key, FORWARD);
        rehash_table(table, new_capacity, table->hash_function_val, BACKWARD);

        table->capacity      = new_capacity;
        table->load_capacity = (size_t)(new_capacity * table->load_factor_threshold);
    }

    const size_t key_index = table->hash_function_key(key) % table->capacity;
    struct bidirectional_hash_table_collision_tree_node* forward_node = get_node_by_key(table, table->collision_trees_forward[key_index], key);

    if (forward_node == NULL) {
        return false;
    }

    struct bidirectional_hash_table_key_value_pair* kv_pair = forward_node->key_value_pair;
    void* val = kv_pair->val;
    const size_t val_index = table->hash_function_val(val) % table->capacity;
   
    struct bidirectional_hash_table_collision_tree_node* backward_node =
        get_node_by_val(table,
            table->collision_trees_backward[val_index],
            val);
    
    // Remove the node from both collision trees
    struct bidirectional_hash_table_collision_tree_node* removed_forward_node  = delete_from_collision_tree(&table->collision_trees_forward[key_index], forward_node);
    struct bidirectional_hash_table_collision_tree_node* removed_backward_node = delete_from_collision_tree(&table->collision_trees_backward[val_index], backward_node);
    
    // Free the key-value pair
    free(kv_pair);
    free(removed_forward_node);
    free(removed_backward_node);
    
    table->size--;
    return true;
}

/**********************************************************************************************
Removes the key-value pair associated with the specified key from the bidirectional hash table.
**********************************************************************************************/
bool bidirectional_hash_table_remove_by_key(struct bidirectional_hash_table* table, void* key) {
    return bidirectional_hash_table_remove_by_key_impl(table, key, true);
}

/************************************************************************************************
Removes the key-value pair associated with the specified value from the bidirectional hash table.
************************************************************************************************/
bool bidirectional_hash_table_remove_by_val(struct bidirectional_hash_table* table, void* val) {
    if (table == NULL || val == NULL) {
        return false;
    }

    if (need_to_shrink_table(table)) {
        size_t new_capacity = table->capacity / 2;

        rehash_table(table, new_capacity, table->hash_function_key, FORWARD);
        rehash_table(table, new_capacity, table->hash_function_val, BACKWARD);

        table->capacity = new_capacity;
        table->load_capacity = (size_t)(new_capacity * table->load_factor_threshold);
    }

    const size_t val_index = table->hash_function_val(val) % table->capacity;

    struct bidirectional_hash_table_collision_tree_node* backward_node = get_node_by_val(table, table->collision_trees_backward[val_index], val);
    
    if (backward_node == NULL) {
        return false;
    }

    struct bidirectional_hash_table_key_value_pair* kv_pair = backward_node->key_value_pair;

    void* key = kv_pair->key;

    const size_t key_index = table->hash_function_key(key) % table->capacity;

    struct bidirectional_hash_table_collision_tree_node* forward_node = 
        get_node_by_key(table,
                        table->collision_trees_forward[key_index],
                        key);

    struct bidirectional_hash_table_collision_tree_node* removed_forward_node  = delete_from_collision_tree(&table->collision_trees_forward [key_index], forward_node);
    struct bidirectional_hash_table_collision_tree_node* removed_backward_node = delete_from_collision_tree(&table->collision_trees_backward[val_index], backward_node);    

    free(removed_forward_node);
    free(removed_backward_node);
    free(kv_pair);

    table->size--;
    return true;
}

/****************************************************************************************
Returns true if the bidirectional hash table contains the specified key, false otherwise.
****************************************************************************************/
bool bidirectional_hash_table_contains_key(struct bidirectional_hash_table* table, void* key) {
    if (table == NULL) {
        return false;
    }

    const size_t key_index = table->hash_function_key(key) % table->capacity;
    struct bidirectional_hash_table_collision_tree_node* node = get_node_by_key(table, table->collision_trees_forward[key_index], key);
    
    return node != NULL;

}

/******************************************************************************************
Returns true if the bidirectional hash table contains the specified value, false otherwise.
******************************************************************************************/
bool bidirectional_hash_table_contains_val(struct bidirectional_hash_table* table, void* val) {
    if (table == NULL) {
        return false;
    }

    const size_t val_index = table->hash_function_val(val) % table->capacity;
    struct bidirectional_hash_table_collision_tree_node* node = get_node_by_val(table, table->collision_trees_backward[val_index], val);
    
    return node != NULL;
}

static size_t find_table_socket_index_starting_from(struct bidirectional_hash_table* table, size_t start_index) {
    for (size_t i = start_index; i < table->capacity; ++i) {
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

    if (table == NULL) {
        return NULL;
    }

    struct bidirectional_hash_table_key_value_pair_iterator* it = malloc(sizeof *it);

    if (it == NULL) {
        return NULL;
    }

    it->table              = table;
    it->table_socket_index = find_first_table_socket_index(table);

    if (it->table_socket_index == SIZE_MAX) {
        // Iterating over an empty table, no key-value pairs to iterate over.
        it->current_tree_node = NULL;
        it->next_tree_node    = NULL;
        return it;
    }

    it->next_tree_node    = find_minimum(table->collision_trees_forward[it->table_socket_index]);
    it->current_tree_node = NULL;

    return it;
}

/*********************************************
Returns true only if there is more to iterate.
*********************************************/
bool bidirectional_hash_table_iterator_has_next(struct bidirectional_hash_table_key_value_pair_iterator* iterator) {
    return iterator != NULL && iterator->next_tree_node != NULL;
}

/**********************************************************************************
Loads the current key/value pair and advances the iteration pointer one pair ahead.
**********************************************************************************/
bool bidirectional_hash_table_iterator_next(struct bidirectional_hash_table_key_value_pair_iterator* iterator, void** pkey, void** pval) {
    if (iterator == NULL ||
        pkey == NULL ||
        pval == NULL ||
        !bidirectional_hash_table_iterator_has_next(iterator)) {
        // Iteration exhausted, no more key-value pairs to iterate over.
        return false;
    }

    struct bidirectional_hash_table_collision_tree_node* n = iterator->next_tree_node;

    *pkey = n->key_value_pair->key;
    *pval = n->key_value_pair->val;

    iterator->current_tree_node = n;

    struct bidirectional_hash_table_collision_tree_node* next = find_successor(n);

    if (next != NULL) {
        iterator->next_tree_node = next;
    } else {
        const size_t next_socket_index = find_table_socket_index_starting_from(iterator->table, iterator->table_socket_index + 1);

        if (next_socket_index == SIZE_MAX) {
            // Iteration exhausted, no more key-value pairs to iterate over.
            iterator->next_tree_node = NULL;
        } else {
            iterator->table_socket_index = next_socket_index;
            iterator->next_tree_node = find_minimum(iterator->table->collision_trees_forward[next_socket_index]);
        }
    }

    return true;
}

/*****************************************************
Removes the most recent key/value pair from the table.
*****************************************************/
bool bidirectional_hash_table_iterator_remove(struct bidirectional_hash_table_key_value_pair_iterator* iterator) {
    if (iterator == NULL || iterator->current_tree_node == NULL) {
        return false;
    }

    void* key      = iterator->current_tree_node->key_value_pair->key;
    void* next_key = iterator->next_tree_node == NULL
                   ? NULL
                   : iterator->next_tree_node->key_value_pair->key;

    if (!bidirectional_hash_table_remove_by_key_impl(iterator->table, key, false)) {
        return false;
    }

    iterator->current_tree_node = NULL;

    if (next_key == NULL) {
        iterator->next_tree_node = NULL;
        return true;
    }

    const size_t next_index = iterator->table->hash_function_key(next_key)
                            % iterator->table->capacity;

    iterator->table_socket_index = next_index;
    iterator->next_tree_node = get_node_by_key(iterator->table, 
                                               iterator->table->collision_trees_forward[next_index],
                                               next_key);

    return iterator->next_tree_node != NULL;
}

/******************************************************************************
Frees the iterator and all associated resources. Does not touch the hash table.
******************************************************************************/
void bidirectional_hash_table_iterator_destroy(struct bidirectional_hash_table_key_value_pair_iterator* iterator) {
    if (iterator == NULL) {
        return;
    }

    if (need_to_shrink_table(iterator->table)) {
        size_t new_capacity = iterator->table->capacity / 2;

        while (new_capacity >= 2 * MINIMUM_CAPACITY && iterator->table->size < (size_t)(new_capacity * iterator->table->load_factor_threshold)) {
            new_capacity /= 2;
        }

        rehash_table(iterator->table, new_capacity, iterator->table->hash_function_key, FORWARD);
        rehash_table(iterator->table, new_capacity, iterator->table->hash_function_val, BACKWARD);

        iterator->table->capacity = new_capacity;
        iterator->table->load_capacity = (size_t)(new_capacity * iterator->table->load_factor_threshold);
    }

    free(iterator);
}

static bool collision_tree_check_invariants(struct bidirectional_hash_table_collision_tree_node* node, 
                                            int (*compare_function)(void*, void*),
                                            bool is_forward) {
    if (node == NULL) {
        return true;
    }

    if (node->left != NULL) {
        if (is_forward) {
            if (compare_function(node->left->key_value_pair->key, node->key_value_pair->key) >= 0) {
                return false;
            }
        } else {
            if (compare_function(node->left->key_value_pair->val, node->key_value_pair->val) >= 0) {
                return false;
            }
        }
    }

    if (node->right != NULL) {
        if (is_forward) {
            if (compare_function(node->right->key_value_pair->key, node->key_value_pair->key) <= 0) {
                return false;
            }
        } else {
            if (compare_function(node->right->key_value_pair->val, node->key_value_pair->val) <= 0) {
                return false;
            }
        }
    }

    return collision_tree_check_invariants(node->left,  compare_function, is_forward) &&
           collision_tree_check_invariants(node->right, compare_function, is_forward);
}

static bool is_balanced(struct bidirectional_hash_table_collision_tree_node* node) {
    if (node == NULL) {
        return true;
    }

    int balance = height(node->left) - height(node->right);

    if (balance < -1 || balance > 1) {
        return false;
    }

    return is_balanced(node->left) && is_balanced(node->right);
}

/**********************************************************************
Returns true if the bidirectional hash table is empty, false otherwise.
**********************************************************************/
bool bidirectional_hash_table_is_empty(struct bidirectional_hash_table* table) {
    return table->size == 0;
}

/*********************************************************************
Returns the number of key-value pairs in the bidirectional hash table.
*********************************************************************/
size_t bidirectional_hash_table_size(struct bidirectional_hash_table* table) {
    return table->size;
}

/***********************************************************************
Returns the capacity of the bidirectional hash table, i.e. the number of
buckets in the hash table.
***********************************************************************/
size_t bidirectional_hash_table_capacity(struct bidirectional_hash_table* table) {
    return table->capacity;
}

/**********************************************************************
Returns true if the bidirectional hash table is valid, false otherwise.
This function is intended for debugging purposes only.
**********************************************************************/
bool bidirectional_hash_table_check_invariants(struct bidirectional_hash_table* table) {
    if (table == NULL) {
        return false;
    }

    for (size_t i = 0; i < table->capacity; ++i) {
        struct bidirectional_hash_table_collision_tree_node* rootf = table->collision_trees_forward [i];
        struct bidirectional_hash_table_collision_tree_node* rootb = table->collision_trees_backward[i];

        if (!collision_tree_check_invariants(rootf, table->compare_function_key, true)) {
            return false;
        }

        if (!collision_tree_check_invariants(rootb, table->compare_function_val, false)) {
            return false;
        }

        if (!is_balanced(rootf) || !is_balanced(rootb)) {
            return false;
        }
    }

    return true;
}
