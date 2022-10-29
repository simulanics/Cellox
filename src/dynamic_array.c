#include "dynamic_array.h"

#include <stdlib.h>

#include "memory.h"

void dynamic_array_free(dynamic_array_t * array)
{
    FREE_ARRAY(value_t, array->values, array->capacity);
    dynamic_array_init(array);
}

void dynamic_array_init(dynamic_array_t * array)
{
    array->values = NULL;
    array->capacity = 0u;
    array->count = 0u;
}

void dynamic_array_write(dynamic_array_t * array, value_t value)
{
    if (array->capacity < array->count + 1u)
    {
        uint32_t oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        value_t * grownArray;
        grownArray = GROW_ARRAY(value_t, array->values, oldCapacity, array->capacity);
        if(!grownArray)
            exit(80);
        array->values = grownArray;
    }
    array->values[array->count] = value;
    array->count++;
}