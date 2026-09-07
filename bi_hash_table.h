#ifndef IO_GITHUB_CODERODDE_C_BIDIRECTIONAL_HASH_TABLE_H
#define IO_GITHUB_CODERODDE_C_BIDIRECTIONAL_HASH_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct bidirectional_hash_table_key_value_pair;
struct bidirectional_hash_table_collision_tree_node;
struct bidirectional_hash_table;
struct bidirectional_hash_table_key_value_pair_iterator;

/******************************************************************
Initializes a bidirectional hash table with the specified capacity.
******************************************************************/
struct bidirectional_hash_table* 
bidirectional_hash_table_create(size_t capacity,
                                float load_factor_threshold,
                                uint64_t (*hash_function_key) (void*),
                                uint64_t (*hash_function_val) (void*),
                                int (*compare_function_key)   (void*, void*),
                                int (*compare_function_val)   (void*, void*));

/*****************************************************************
Destroys a bidirectional hash table and frees all allocated memory.                                                     
*****************************************************************/
void bidirectional_hash_table_destroy(struct bidirectional_hash_table* table);

/************************************************************************************
Inserts a key-value pair into the bidirectional hash table. If the key-value mapping  
exists, does nothing. If either the key or the value is already present in the table, 
it will be replaced with the new mapping. Returns true only if the hash table has
changed. False otherwise.
************************************************************************************/
bool bidirectional_hash_table_insert(struct bidirectional_hash_table* table, void* key, void* val);

/*********************************************************************************
Finds the value associated with the specified key in the bidirectional hash table.
*********************************************************************************/
void* bidirectional_hash_table_find_by_key(struct bidirectional_hash_table* table, void* key);

/*********************************************************************************
Finds the key associated with the specified value in the bidirectional hash table.
*********************************************************************************/
void* bidirectional_hash_table_find_by_val(struct bidirectional_hash_table* table, void* val);

/**********************************************************************************************
Removes the key-value pair associated with the specified key from the bidirectional hash table.
**********************************************************************************************/
bool bidirectional_hash_table_remove_by_key(struct bidirectional_hash_table* table, void* key);

/************************************************************************************************
Removes the key-value pair associated with the specified value from the bidirectional hash table.
************************************************************************************************/
bool bidirectional_hash_table_remove_by_val(struct bidirectional_hash_table* table, void* val);

/****************************************************************************************
Returns true if the bidirectional hash table contains the specified key, false otherwise.
****************************************************************************************/
bool bidirectional_hash_table_contains_key(struct bidirectional_hash_table* table, void* key);

/******************************************************************************************
Returns true if the bidirectional hash table contains the specified value, false otherwise.
******************************************************************************************/
bool bidirectional_hash_table_contains_val(struct bidirectional_hash_table* table, void* val);

/*********************************************************
Creates an iterator over the hash table's key/value pairs.
*********************************************************/
struct bidirectional_hash_table_key_value_pair_iterator* bidirectional_hash_table_create_iterator(struct bidirectional_hash_table* table);

/*********************************************
Returns true only if there is more to iterate.
*********************************************/
bool bidirectional_hash_table_iterator_has_next(struct bidirectional_hash_table_key_value_pair_iterator* iterator);

/**********************************************************************************
Loads the current key/value pair and advances the iteration pointer one pair ahead.
**********************************************************************************/
int bidirectional_hash_table_iterator_next(struct bidirectional_hash_table_key_value_pair_iterator* iterator, void** pkey, void** pval);

/*****************************************************
Removes the most recent key/value pair from the table.
*****************************************************/
int bidirectional_hash_table_iterator_remove(struct bidirectional_hash_table_key_value_pair_iterator* iterator);

#endif // IO_GITHUB_CODERODDE_C_BIDIRECTIONAL_HASH_TABLE_H