#ifndef CELLOX_TABLE_H_
#define CELLOX_TABLE_H_

#include "common.h"
#include "value.h"

/// @brief An entry in a hashtable
/// @details An Entry in hashtable contains a key, that is used to look up the entry in O(n)
typedef struct
{
    /// Key of the entry 🔑
    object_string_t * key;
    /// The value that is associated with the key
    value_t value;
} hash_table_entry_t;

/// @brief A hashtable
/// @details The hashtable uses seperate chaining if a hashcollision occurs
typedef struct
{
    /// Number of entries in the hashtable
    uint32_t count;
    /// The capacity of the hashtable
    uint32_t capacity;
    /// Pointer to the first entry that is stored in the hashtable
    hash_table_entry_t * entries;
} hash_table_t;

/// @brief Dealocates the memory used by the hashtable
/// @param table The table where the contents are freed
void hash_table_free(hash_table_t * table);

/// @brief Initializes the hashtable
/// @param table The hashtable that is initialized
void hash_table_init(hash_table_t * table);

/// @brief Marks all the objects in the hashtable
/// @param table The hashtable where all the objects are marked
void hash_table_mark(hash_table_t * table);

/// @brief Copys all the entries from a table to another table
/// @param from The source-table
/// @param to The destination-table
void hash_table_add_all(hash_table_t * from, hash_table_t * to);

/// @brief Attempts to Deletes an entry in the hashtable and returns true if an entry coresponding to the given key has been found
/// @param table The table where an attempt is made to delete an entry
/// @param key The key of the value that is deleted
/// @return A boolean value that indicates whether a value was deleted
bool hash_table_delete(hash_table_t * table, object_string_t * key);

/// @brief Looks up a string in the hashtable
/// @param table The table where the string is searched
/// @param chars The underlying character representation of the string
/// @param length The length of the string
/// @param hash The hashvalue of the string
/// @return An object_string_t or NULL if the string hasn't been found
object_string_t * hash_table_find_string(hash_table_t * table, char const * chars, uint32_t length, uint32_t hash);

/// @brief Reads the Value to the specified key, if an entry corresponding to the given key is present 
/// @param table The table where the entry is looked upo
/// @param key The key that is used for searching for the entry
/// @param value Stores the value corresponding to the key in the passed value parameter
/// @return true if an entry coresponding to the given key has been found
bool hash_table_get(hash_table_t * table, object_string_t * key, value_t * value);

/// @brief Removes the strings that are not referenced anymore from the table
/// @param table The table where all the values marked as white (not reachable) are removed
void hash_table_remove_white(hash_table_t * table);

/// @brief Changes the value corresponding to the key or creates a new entry if no entry corespronding to the key has been found
/// @param table The table where the entry is changed or inserted
/// @param key The key of the entry that is changed or the  key of the new entry
/// @param value The value the value of the entry is changed to or value of the new entry
/// @return true if an entry coresponding to the given key has been found
bool hash_table_set(hash_table_t * table, object_string_t * key, value_t value);

#endif
