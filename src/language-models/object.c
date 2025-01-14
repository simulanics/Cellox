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
 * @file object.c
 * @brief File containing implementation of functionalitity regarding cellox objects.
 */

#include "object.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../backend/memory_mutator.h"
#include "../backend/virtual_machine.h"
#include "../string_utils.h"

/// Marko for allocating a new object
#define ALLOCATE_OBJECT(type, objectType) (type *)object_allocate_object(sizeof(type), objectType)

/// The object types of cellox as a string
static char const * objectTypesStringified[] = {"method",          "class",  "closure", "array",  "function",
                                                "native function", "string", "upvalue", "unknown"};

static object_t * object_allocate_object(size_t, object_type);
static object_string_t * object_allocate_string(char *, uint32_t, uint32_t);
static void object_print_function(object_function_t *);

object_string_t * object_copy_string(char const * chars, uint32_t length, bool removeBackSlash) {
    uint32_t hash;
    object_string_t * interned;
    char * heapChars;

    if (!string_utils_contains_character_restricted(chars, '\\', length)) {
        hash = string_utils_hash_string(chars, length);
        interned = value_hash_table_find_string(&virtualMachine.strings, chars, length, hash);
        if (interned) {
            return interned;
        }
        heapChars = ALLOCATE(char, length + 1);
        memcpy(heapChars, chars, length);
        heapChars[length] = '\0';
    } else {
        heapChars = ALLOCATE(char, length + 1);
        memcpy(heapChars, chars, length);
        heapChars[length] = '\0';
        char * next = NULL;
        for (uint32_t i = 0; i < length; i++) {
            if (heapChars[i] == '\\') {
                if (string_utils_resolve_escape_sequence(&heapChars[i], &length)) {
                    return NULL;
                }
            }
        }
        // We have to look again for duplicates in the hashtable storing the strings allocated by the virtualMachine
        hash = string_utils_hash_string(heapChars, length);
        interned = value_hash_table_find_string(&virtualMachine.strings, heapChars, length, hash);
        if (interned) {
            free(heapChars);
            return interned;
        }
    }

    return object_allocate_string(heapChars, strlen(heapChars), hash);
}

object_bound_method_t * object_new_bound_method(value_t receiver, object_closure_t * method) {
    object_bound_method_t * bound = ALLOCATE_OBJECT(object_bound_method_t, OBJECT_BOUND_METHOD);
    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

object_class_t * object_new_class(object_string_t * name) {
    object_class_t * celloxClass = ALLOCATE_OBJECT(object_class_t, OBJECT_CLASS);
    celloxClass->name = name;
    value_hash_table_init(&celloxClass->methods);
    return celloxClass;
}

object_dynamic_value_array_t * object_new_dynamic_value_array() {
    object_dynamic_value_array_t * array = ALLOCATE_OBJECT(object_dynamic_value_array_t, OBJECT_ARRAY);
    dynamic_value_array_init(&array->array);
    return array;
}

object_closure_t * object_new_closure(object_function_t * function) {
    object_upvalue_t ** upvalues = ALLOCATE(object_upvalue_t *, function->upvalueCount);
    for (uint32_t i = 0; i < function->upvalueCount; i++) {
        upvalues[i] = NULL;
    }
    object_closure_t * closure = ALLOCATE_OBJECT(object_closure_t, OBJECT_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    return closure;
}

object_function_t * object_new_function() {
    object_function_t * function = ALLOCATE_OBJECT(object_function_t, OBJECT_FUNCTION);
    function->arity = 0u;
    function->upvalueCount = 0u;
    function->name = NULL;
    chunk_init(&function->chunk);
    return function;
}

object_instance_t * object_new_instance(object_class_t * celloxClass) {
    object_instance_t * instance = ALLOCATE_OBJECT(object_instance_t, OBJECT_INSTANCE);
    instance->celloxClass = celloxClass;
    value_hash_table_init(&instance->fields);
    return instance;
}

object_native_t * object_new_native(native_function_t function) {
    object_native_t * native = ALLOCATE_OBJECT(object_native_t, OBJECT_NATIVE);
    native->function = function;
    return native;
}

object_upvalue_t * object_new_upvalue(value_t * slot) {
    // Allocating the memory used by the upvalue
    object_upvalue_t * upvalue = ALLOCATE_OBJECT(object_upvalue_t, OBJECT_UPVALUE);
    // We zero out the closed field of the upvalue when we create it
    upvalue->closed = NULL_VAL;
    // Adress of the slot where the closed over variables live (enclosing environment)
    upvalue->location = slot;
    // When we allocate a new upvalue, it is not attached to any list
    upvalue->next = NULL;
    return upvalue;
}

void object_print(value_t value) {
    switch (OBJECT_TYPE(value)) {
    case OBJECT_ARRAY:
        {
            object_dynamic_value_array_t * array = AS_ARRAY(value);
            putc('{', stdout);
            for (size_t i = 0; i < array->array.count; i++) {
                value_print(array->array.values[i]);
                if (i != array->array.count - 1) {
                    printf(", ");
                }
            }
            putc('}', stdout);
            break;
        }
    case OBJECT_BOUND_METHOD:
        object_print_function(AS_BOUND_METHOD(value)->method->function);
        break;
    case OBJECT_CLASS:
        printf("%s", AS_CLASS(value)->name->chars);
        break;
    case OBJECT_CLOSURE:
        object_print_function(AS_CLOSURE(value)->function);
        break;
    case OBJECT_FUNCTION:
        object_print_function(AS_FUNCTION(value));
        break;
    case OBJECT_INSTANCE:
        {
            object_instance_t * instance = AS_INSTANCE(value);
            if (!instance->fields.count) {
                printf("{}");
                break;
            }
            putc('{', stdout);
            value_t fieldValue;
            size_t fieldCounter = instance->fields.count;
            for (size_t i = 0; i < instance->fields.capacity; i++) {
                if (instance->fields.entries[i].key != NULL) {
                    printf("%s: ", instance->fields.entries[i].key->chars);
                    if (IS_STRING(instance->fields.entries[i].value)) {
                        putc('"', stdout);
                    }
                    value_print(instance->fields.entries[i].value);
                    if (IS_STRING(instance->fields.entries[i].value)) {
                        putc('"', stdout);
                    }
                    if (fieldCounter-- > 1) {
                        printf(", ");
                    }
                }
            }
            putc('}', stdout);
            break;
        }
    case OBJECT_NATIVE:
        printf("<native fn>");
        break;
    case OBJECT_STRING:
        printf("%s", AS_CSTRING(value));
        break;
    case OBJECT_UPVALUE:
        printf("upvalue");
        break;
    }
}

object_string_t * object_take_string(char * chars, uint32_t length) {
    uint32_t hash = string_utils_hash_string(chars, length);
    object_string_t * interned = value_hash_table_find_string(&virtualMachine.strings, chars, length, hash);
    if (interned) {
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }
    return object_allocate_string(chars, length, hash);
}

/// @brief Creates a string allocates memory to store a string
/// @param chars Pointer to the start of the string
/// @param length The length of the string
/// @param hash The hashvalue of the string
/// @return The created string
static object_string_t * object_allocate_string(char * chars, uint32_t length, uint32_t hash) {
    object_string_t * string = ALLOCATE_OBJECT(object_string_t, OBJECT_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;
    virtual_machine_push(OBJECT_VAL(string));
    // Adds the string to hashtable storing all the strings allocated by the virtualMachine
    value_hash_table_set(&virtualMachine.strings, string, NULL_VAL);
    virtual_machine_pop();
    return string;
}

/// @brief Allocates the memory for an object of a given type
/// @param size The size of the object that is allocated
/// @param type The type of the allocated object
/// @return The allocated object
static object_t * object_allocate_object(size_t size, object_type type) {
    // Allocates the memory used by the Object
    object_t * object = (object_t *)memory_mutator_reallocate(NULL, 0, size);
    // Sets the type of the object
    object->type = type;
    // Disables mark so it is picked up by the Garbage Collection in the next cycle
    object->isMarked = false;
    // Adds the object at the start of the linked list storing the objects allocated by the virtualMachine
    object->next = virtualMachine.objects;
    virtualMachine.objects = object;
#ifdef DEBUG_LOG_GC
    printf("%p allocated %zu bytes for %d\n", (void *)object, size, type);
#endif
    return object;
}

/// @brief Prints a function or a script
/// @param function The function that is printed
static void object_print_function(object_function_t * function) {
    if (!function->name) {
        // top level code
        printf("<script>");
        return;
    }
    // A function
    printf("<fun %s>", function->name->chars);
}

char const * object_stringify_type(object_t * object) {
    switch (object->type) {
    case OBJECT_BOUND_METHOD:
        return objectTypesStringified[0];
    case OBJECT_CLASS:
        return objectTypesStringified[1];
    case OBJECT_CLOSURE:
        return objectTypesStringified[2];
    case OBJECT_ARRAY:
        return objectTypesStringified[3];
    case OBJECT_FUNCTION:
        return objectTypesStringified[4];
    case OBJECT_INSTANCE:
        return ((object_instance_t *)object)->celloxClass->name->chars;
        ;
    case OBJECT_NATIVE:
        return objectTypesStringified[5];
    case OBJECT_STRING:
        return objectTypesStringified[6];
    case OBJECT_UPVALUE:
        return objectTypesStringified[7];
    default:
        return objectTypesStringified[8];
        ;
    }
}
