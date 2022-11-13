#ifndef CELLOX_MEMORY_H_
#define CELLOX_MEMORY_H_

#include "common.h"
#include "object.h"

// Makro that allocates the memory needed for a given type multiplied by the count
#define ALLOCATE(type, count) \
    (type *)memory_reallocate(NULL, 0, sizeof(type) * (count))

// Makro that frees the memory used by a given type at the position specified by the pointer
#define FREE(type, pointer) \
    memory_reallocate(pointer, sizeof(type), 0)

// Makro that determines the increase in capacity for a dynamic array (initalizes capacity at 8)
#define GROW_CAPACITY(capacity) \
    ((capacity) < 8u ? 8u : (capacity) + 2u)

// Makro that increases the size of a dynamic Array
#define GROW_ARRAY(type, pointer, oldCount, newCount) \
    (type *)memory_reallocate(pointer, sizeof(type) * (oldCount), sizeof(type) * (newCount))

// Makro that dealocates an existing dynamic array
#define FREE_ARRAY(type, pointer, oldCount) \
    memory_reallocate(pointer, sizeof(type) * (oldCount), 0)

/** @brief Starts the garbage collection process.
 * @details The garbage collector of cellox is a precise GC.
 * That means that the garbage collector knows whether words in memory are pointers
 * and which store a value - like a string, boolean or a number.
 * The Algorithm that is used is called mark-and-sweep.
 * It consists of two phases:
 * 1. Mark phase
 * 2. Sweep phase
 * In the marking phase we start at the roots and traverse through all the objects the roots refer to.
 * In the sweeping phase all the reachable objects have been marked, and therefore we can reclaim the memory that is used by the unmarked objects.
 */
void memory_collect_garbage();

/// @brief Dealocates the memory used by the objects of the virtualMachine
void memory_free_objects();

/// @brief Marks a cellox object
/// @param object The object that is marked
void memory_mark_object(object_t * object);

/// @brief Marks a cellox value
/// @param value The value that is marked
void memory_mark_value(value_t value);

/// @brief Reallocates a block in memory
/// @param pointer Pointer to the memory block that is reallocated
/// @param oldSize The oldsize if the memory block
/// @param newSize The new size of the memory block
/// @return The reallocated memory block
void * memory_reallocate(void * pointer, size_t oldSize, size_t newSize);

#endif  
