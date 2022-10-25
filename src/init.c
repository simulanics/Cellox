#include "init.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CELLOX_TESTS_RUNNING
#include "cellox_config.h"
#endif

#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "virtual_machine.h"

// Maximum length of a line is 1024 characters
#define MAX_LINE_LENGTH 1024u

// Reads a lox program from a file
static char *init_read_file(char const *);

/* Run with repl
 * 1. Read the user input
 * 2. Evaluate your code
 * 3. Print any results
 * 4. Loop back to step 1
 */
static void init_repl();

// Reads a lox program from a file and executes the program
static void init_run_from_file(char const *);

void init_initialize(int const argc, char const ** argv)
{
    vm_init();
    if (argc == 1)
        init_repl();
    else if (argc == 2)
        init_run_from_file(argv[1]);
    else
    {
        // Too much arguments (>1) TODO: Add argumenrs for the compiler e.g. --analyze/-a, --help, --store/-s and --version/-v options 
        fprintf(stderr, "Usage: Cellox [path]\n");        
        vm_free();
        exit(64);
    }
    vm_free();
}

static char * init_read_file(char const * path)
{
    // Opens a file of a nonspecified format (b) in read mode (r)
    FILE * file = fopen(path, "rb");
    if (file == NULL)
    {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }
    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);
    char * buffer = (char *)malloc(fileSize + 1);
    if (buffer == NULL)
    {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }
    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize)
    {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }
    // We add null the end of the source-code to mark the end of the file
    buffer[bytesRead] = '\0';
    fclose(file);
    return buffer;
}

static void init_repl()
{
    // Used to store the next line that read from input
    char line[MAX_LINE_LENGTH];
#ifndef CELLOX_TESTS_RUNNING
    printf("   _____     _ _           \n  / ____|   | | |          \n | |     ___| | | _____  __\n | |    / _ \\ | |/ _ \\ \\/ /\n | |___|  __/ | | (_) >  < \n  \\_____\\___|_|_|\\___/_/\\_\\\n");
    printf("\t\t Version %i.%i\n", CELLOX_VERSION_MAJOR, CELLOX_VERSION_MINOR);
#endif
    for (;;)
    {
        // Prints command prompt
        printf("> ");
        // Reads the next line that was input by the user and stores
        if (!fgets(line, sizeof(line), stdin))
        {
            printf("\n");
            break;
        }
        // We close the command prompt if the last input was empty - \n
        if (strlen(line) == 1)
            exit(0);
        vm_interpret(line);
    }
}

static void init_run_from_file(char const * path)
{
    char * source = init_read_file(path);
    InterpretResult result = vm_interpret(source);
    free(source);
    #ifndef CELLOX_TESTS_RUNNING
    if(result != INTERPRET_OK)
        vm_free();
    // Error during compilation process
    if (result == INTERPRET_COMPILE_ERROR)
        exit(65);
    // Error during runtime
    if (result == INTERPRET_RUNTIME_ERROR)
        exit(70);
    #endif
}
