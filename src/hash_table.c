/****************************************************************************
 * Copyright (C) 2022 by Frederik Tobner                                    *
 *                                                                          *
 * This file is part of Cellox.                                             *
 *                                                                          *
 * Permission to use, copy, modify, and distribute this software and its    *
 * documentation under the terms of the GNU General Public License is       *
 * hereby granted.                                                          *
 * No representations are made about the suitability of this software for   *
 * any purpose.                                                             *
 * It is provided "as is" without express or implied warranty.              *
 * See the <https://www.gnu.org/licenses/gpl-3.0.html/>GNU General Public   *
 * License for more details.                                                *
 ****************************************************************************/

/**
 * @file hash_table.c
 * @brief File containing the hashtable implementation used internally by the interpreter
 */
#include "hash_table.h"

#include <stdlib.h>
#include <string.h>

#include "garbage_collector.h"
#include "memory_mutator.h"
#include "object.h"

/// @brief The max load factor of the hashtable
/// @details If the max load factor multiplied with the capacity is reached we grow the hashtable
#define TABLE_MAX_LOAD 0.75

static void hash_table_adjust_capacity(hash_table_t *, int32_t);
static hash_table_entry_t * hash_table_find_entry(hash_table_entry_t *, int32_t, object_string_t *);

void hash_table_free(hash_table_t * table)
{
    FREE_ARRAY(hash_table_entry_t, table->entries, table->capacity);
    hash_table_init(table);
}

void hash_table_init(hash_table_t * table)
{
    table->count = table->capacity = 0;
    table->entries = NULL;
}

void hash_table_mark(hash_table_t * table)
{
    for (uint32_t i = 0; i < table->capacity; i++)
    {
        hash_table_entry_t * entry = table->entries + i;
        garbage_collector_mark_object((object_t *)entry->key);
        garbage_collector_mark_value(entry->value);
    }
}

void hash_table_add_all(hash_table_t * from, hash_table_t * to)
{
    for (uint32_t i = 0; i < from->capacity; i++)
    {
        hash_table_entry_t * entry = from->entries + i;
        if (entry->key)
            hash_table_set(to, entry->key, entry->value);
    }
}

bool hash_table_delete(hash_table_t * table, object_string_t * key)
{
    if (!table->count)
        return false;
    // Find the entry.
    hash_table_entry_t * entry = hash_table_find_entry(table->entries, table->capacity, key);
    if (!entry->key)
        return false;
    // Replace the entry with a tombstone
    entry->key = NULL;
    entry->value = BOOL_VAL(true);
    return true;
}

object_string_t * hash_table_find_string(hash_table_t * table, char const * chars, uint32_t length, uint32_t hash)
{
    if (!table->count)
        return NULL;

    uint32_t index = hash & (table->capacity - 1);
    for (;;)
    {
        hash_table_entry_t * entry = &table->entries[index];
        if (!entry->key)
        {
            // Stop if we find an empty non-tombstone entry.
            if (IS_NULL(entry->value))
                return NULL;
        }
        else if (entry->key->length == length && entry->key->hash == hash && !memcmp(entry->key->chars, chars, length))
            return entry->key;  /// We found the string
        // We look in the next bucket but eventually we also have to wrap around the array when we reach the end
        index = (index + 1) & (table->capacity - 1);
    }
}

bool hash_table_get(hash_table_t * table, object_string_t * key, value_t * value)
{
    if (!table->count)
        return false;

    hash_table_entry_t * entry = hash_table_find_entry(table->entries, table->capacity, key);
    // The entry we found does not correspond to the key
    if (!entry->key)
        return false;

    *value = entry->value;
    return true;
}

void hash_table_remove_white(hash_table_t * table)
{
    for (uint32_t i = 0; i < table->capacity; i++)
    {
        hash_table_entry_t * entry = &table->entries[i];
        if (entry->key && !entry->key->obj.isMarked)
            hash_table_delete(table, entry->key);
    }
}

bool hash_table_set(hash_table_t * table, object_string_t * key, value_t value)
{
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD)
    {
        uint32_t capacity = GROW_HASHTABLE_CAPACITY(table->capacity);
        hash_table_adjust_capacity(table, capacity);
    }
    hash_table_entry_t * entry = hash_table_find_entry(table->entries, table->capacity, key);
    bool isNewKey = !entry->key;
    if (isNewKey && IS_NULL(entry->value))
        table->count++;
    entry->key = key;
    entry->value = value;
    return isNewKey;
}

/// @brief Adjusts the capicity of a hashtable
/// @param table The hashtable where the capacity is changed
/// @param capacity The new capacity of the hashtable
/// @details  We grow the hashtable when it becomes 75% is filled, 
/// so we can wrap around the entries when we look for a key,
/// without risking an infinite loop when the hashtable is full.
static void hash_table_adjust_capacity(hash_table_t * table, int32_t capacity)
{
    hash_table_entry_t * entries = ALLOCATE(hash_table_entry_t, capacity);
    for (uint32_t i = 0; i < capacity; i++)
    {
        entries[i].key = NULL;
        entries[i].value = NULL_VAL;
    }
    table->count = 0;
    for (uint32_t i = 0; i < table->capacity; i++)
    {
        hash_table_entry_t * entry = table->entries + i;
        if (!entry->key)
            continue;
        hash_table_entry_t * dest = hash_table_find_entry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }
    FREE_ARRAY(hash_table_entry_t, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

/// @brief Looks up an entry in the hashtable
/// @param entries The entries of the hashtable that is searched
/// @param capacity The capacity of the hashtable
/// @param key The key of the hashtable that is looked up
/// @return Returns the entry or NULL if the value has already been deleted
static hash_table_entry_t * hash_table_find_entry(hash_table_entry_t * entries, int32_t capacity, object_string_t * key)
{
    uint32_t index = key->hash & (capacity - 1);
    hash_table_entry_t *tombstone = NULL;
    for (;;)
    {
        hash_table_entry_t * entry = &entries[index];
        if (!entry->key)
        {
            if (IS_NULL(entry->value))
                /// Empty entry.
                return tombstone != NULL ? tombstone : entry;
            else
            {
                /* We found a tombstone. 😱
                 * A tombstone marks the slot of a value that has already been deleted.
                 * This is done so because when we look up an entry that has been moved by a collision,
                 * the entry that has occupied the slot where the collision occured has been deleted,
                 * so we need a value to indicate that another value prevouisly occupied the slot so we don't stop looking for the entry
                 */
                if (!tombstone)
                    tombstone = entry;
            }
        }
        else if (entry->key == key)
            return entry; // We found the key 🔑
        index = (index + 1) & (capacity - 1);
    }
}
