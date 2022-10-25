#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "memory.h"
#include "lexer.h"
#include "virtual_machine.h"

// The debug header file only needs to be included if the DEBUG_CODE is defined
#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

// Type definition of the parser structure
typedef struct
{
    // The token that is currently being parsed
    Token current;
    // The token that was previously parsed
    Token previous;
    // Flag that indicates whether an error occured during the compilation
    bool hadError;
    // Flag that indicates that the compiler couldn't synchronize after an errror occured
    bool panicMode;
} Parser;

// Precedences of the Tokens
typedef enum
{
    PREC_NONE,
    PREC_ASSIGNMENT, // = += -= *= /= %= **=
    PREC_OR,         // or ||
    PREC_AND,        // and  &&
    PREC_EQUALITY,   // == !=
    PREC_COMPARISON, // < > <= >=
    PREC_TERM,       // + -
    PREC_FACTOR,     // * / % **
    PREC_UNARY,      // ! -
    PREC_CALL,       // . () []
    PREC_PRIMARY
} Precedence;

typedef void (* ParseFn)(bool canAssign);

// Type definition of a parsing rule structure
typedef struct
{
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

// Type definition of a local variable structure
typedef struct
{
    // Name of the local variable
    Token name;
    // Scope depth where the local variable was declared
    int32_t depth;
    // Boolean value that determines whether the local variable is captured by a closure
    bool isCaptured;
} Local;

// Type definition of an upvalue structure
typedef struct
{
    // Index of the upvalue
    uint8_t index;
    // Flag that indicates whether the value is a local value
    bool isLocal;
} Upvalue;

// Types of a function - either a script or a function
typedef enum
{
    TYPE_FUNCTION,
    TYPE_INITIALIZER,
    TYPE_METHOD,
    TYPE_SCRIPT
} FunctionType;

// Type definition of the compiler structure
typedef struct Compiler
{
    struct Compiler * enclosing;
    ObjectFunction * function;
    FunctionType type;
    Local locals[UINT8_COUNT];
    int32_t localCount;
    Upvalue upvalues[UINT8_COUNT];
    int32_t scopeDepth;
} Compiler;

typedef struct ClassCompiler
{
    struct ClassCompiler *enclosing;
    bool hasSuperclass;
} ClassCompiler;

// Global parser variable
Parser parser;

// Global compiler variable
Compiler * current = NULL;

// Global classCompiler variable
ClassCompiler * currentClass = NULL;

static void compiler_add_local(Token);
static uint32_t compiler_add_upvalue(Compiler *, uint8_t, bool);
static void compiler_advance();
static void compiler_and(bool);
static uint8_t compiler_argument_list();
static void compiler_begin_scope();
static void compiler_binary(bool);
static void compiler_block();
static void compiler_call(bool);
static bool compiler_check(TokenType);
static void compiler_class_declaration();
static void compiler_consume(TokenType, char const *);
static Chunk *compiler_current_chunk();
static void compiler_declaration();
static void compiler_declare_variable();
static void compiler_define_variable(uint8_t);
static void compiler_dot(bool);
static void compiler_emit_byte(uint8_t);
static void compiler_emit_bytes(uint8_t, uint8_t);
static void compiler_emit_constant(Value);
static int32_t compiler_emit_jump(uint8_t);
static void compiler_emit_loop(int32_t);
static void compiler_emit_return();
static ObjectFunction *compiler_end();
static void compiler_end_scope();
static void compiler_error(char const *);
static void compiler_error_at(Token *, char const *);
static void compiler_error_at_current(char const *);
static void compiler_expression();
static void compiler_expression_statement();
static void compiler_for_statement();
static void compiler_function(FunctionType);
static void compiler_function_declaration();
static ParseRule *compiler_get_rule(TokenType);
static void compiler_grouping(bool);
static uint8_t compiler_identifier_constant(Token *);
static bool compiler_identifiers_equal(Token *, Token *);
static void compiler_if_statement();
static void compiler_init(Compiler *, FunctionType);
static void compiler_index_of(bool);
static void compiler_literal(bool);
static void compiler_mark_initialized();
static uint8_t compiler_make_constant(Value);
static bool compiler_match_token(TokenType);
static void compiler_method();
static void compiler_named_variable(Token, bool);
static void compiler_nondirect_assignment(uint8_t, uint8_t, uint8_t, uint8_t);
static void compiler_number(bool);
static void compiler_or(bool);
static void compiler_parse_precedence(Precedence);
static uint8_t compiler_parse_variable(char const *);
static void compiler_patch_jump(int32_t);
static void compiler_print_statement();
static int32_t compiler_resolve_local(Compiler *, Token *);
static int32_t compiler_resolve_upvalue(Compiler *, Token *);
static void compiler_return_statement();
static void compiler_statement();
static void compiler_string(bool);
static void compiler_super(bool);
static void compiler_synchronize();
static Token compiler_synthetic_token(char const *);
static void compiler_this(bool);
static void compiler_unary(bool);
static void compiler_var_declaration();
static void compiler_variable(bool);
static void compiler_while_statement();

ObjectFunction * compiler_compile(char const * source)
{
    lexer_init(source);
    Compiler compiler;
    compiler_init(&compiler, TYPE_SCRIPT);
    parser.hadError = false;
    parser.panicMode = false;
    compiler_advance();
    // We keep compiling until we hit the end of the source file
    while (!compiler_match_token(TOKEN_EOF))
        compiler_declaration();
    ObjectFunction *function = compiler_end();
    return parser.hadError ? NULL : function;
}

void compiler_mark_roots()
{
    Compiler * compiler = current;
    while (compiler != NULL)
    {
        memory_mark_object((Object *)compiler->function);
        compiler = compiler->enclosing;
    }
}

/* ParseRules for the language
 * Prefix - at the beginning of a statement
 * Infix - in the middle/end of a statement
 * Precedence
*/
ParseRule rules[] = {
    [TOKEN_AND] = {NULL, compiler_and, PREC_AND},
    [TOKEN_BANG] = {compiler_unary, NULL, PREC_UNARY},
    [TOKEN_BANG_EQUAL] = {NULL, compiler_binary, PREC_EQUALITY},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, compiler_dot, PREC_CALL},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL] = {NULL, NULL, PREC_ASSIGNMENT},
    [TOKEN_EQUAL_EQUAL] = {NULL, compiler_binary, PREC_EQUALITY},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE] = {compiler_literal, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_FUN] = {NULL, NULL, PREC_NONE},
    [TOKEN_GREATER] = {NULL, compiler_binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL, compiler_binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER] = {compiler_variable, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_PAREN] = {compiler_grouping, compiler_call, PREC_CALL},
    [TOKEN_LEFT_BRACKET] = {NULL, compiler_index_of, PREC_CALL},
    [TOKEN_LESS] = {NULL, compiler_binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL] = {NULL, compiler_binary, PREC_COMPARISON},
    [TOKEN_MODULO] = {NULL, compiler_binary, PREC_FACTOR},
    [TOKEN_MODULO_EQUAL] = {NULL, NULL, PREC_ASSIGNMENT},
    [TOKEN_MINUS] = {compiler_unary, compiler_binary, PREC_TERM},
    [TOKEN_MINUS_EQUAL] = {NULL, NULL, PREC_ASSIGNMENT},
    [TOKEN_NULL] = {compiler_literal, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {compiler_number, NULL, PREC_NONE},
    [TOKEN_PLUS] = {NULL, compiler_binary, PREC_TERM},
    [TOKEN_PLUS_EQUAL] = {NULL, NULL, PREC_ASSIGNMENT},
    [TOKEN_OR] = {NULL, compiler_or, PREC_OR},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_NONE},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_SLASH] = {NULL, compiler_binary, PREC_FACTOR},
    [TOKEN_SLASH_EQUAL] = {NULL, NULL, PREC_ASSIGNMENT},
    [TOKEN_STAR] = {NULL, compiler_binary, PREC_FACTOR},
    [TOKEN_STAR_EQUAL] = {NULL, NULL, PREC_ASSIGNMENT},
    [TOKEN_STAR_STAR] = {NULL, compiler_binary, PREC_FACTOR},
    [TOKEN_STRING] = {compiler_string, NULL, PREC_NONE},
    [TOKEN_SUPER] = {compiler_super, NULL, PREC_NONE},
    [TOKEN_THIS] = {compiler_this, NULL, PREC_NONE},
    [TOKEN_TRUE] = {compiler_literal, NULL, PREC_NONE},
    [TOKEN_VAR] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
};

// Adds a new local variable to the stack
static void compiler_add_local(Token name)
{
    if (current->localCount == UINT8_COUNT)
    {
        // We can only have 256 objects on the stack 😔
        compiler_error("Too many local variables in function.");
        return;
    }
    Local *local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;
    local->isCaptured = false;
}

// Adds an upValue to the compiler
static uint32_t compiler_add_upvalue(Compiler * compiler, uint8_t index, bool isLocal)
{
    uint32_t upvalueCount = compiler->function->upvalueCount;

    for (uint32_t i = 0; i < upvalueCount; i++)
    {
        Upvalue *upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal)
            return i;
    }
    if (upvalueCount == UINT8_COUNT)
    {
        compiler_error("Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    return compiler->function->upvalueCount++;
}

// Advances a poosition further in the linea sequence of tokens
static void compiler_advance()
{
    parser.previous = parser.current;

    for (;;)
    {
        parser.current = scan_token();
        if (parser.current.type != TOKEN_ERROR)
            break;

        compiler_error_at_current(parser.current.start);
    }
}

// Handles an and-expression
static void compiler_and(bool canAssign)
{
    int32_t endJump = compiler_emit_jump(OP_JUMP_IF_FALSE);
    compiler_emit_byte(OP_POP);
    compiler_parse_precedence(PREC_AND);
    compiler_patch_jump(endJump);
}

// Compiles a List of argument of a call expression to bytecode instructions
static uint8_t compiler_argument_list()
{
    uint8_t argCount = 0;
    if (!compiler_check(TOKEN_RIGHT_PAREN))
    {
        do
        {
            compiler_expression();
            if (argCount == 255)
                compiler_error("Can't have more than 254 arguments.");
            argCount++;
        } while (compiler_match_token(TOKEN_COMMA));
    }
    compiler_consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}

// Handles the beginning of a new Scope
static void compiler_begin_scope()
{
    current->scopeDepth++;
}

// Compiles a binary expression
static void compiler_binary(bool canAssign)
{
    TokenType operatorType = parser.previous.type;
    ParseRule * rule = compiler_get_rule(operatorType);
    compiler_parse_precedence((Precedence)(rule->precedence + 1));

    switch (operatorType)
    {
    case TOKEN_BANG_EQUAL:
        compiler_emit_bytes(OP_EQUAL, OP_NOT);
        break;
    case TOKEN_EQUAL_EQUAL:
        compiler_emit_byte(OP_EQUAL);
        break;
    case TOKEN_GREATER:
        compiler_emit_byte(OP_GREATER);
        break;
    case TOKEN_GREATER_EQUAL:
        compiler_emit_bytes(OP_LESS, OP_NOT);
        break;
    case TOKEN_LESS:
        compiler_emit_byte(OP_LESS);
        break;
    case TOKEN_LESS_EQUAL:
        compiler_emit_bytes(OP_GREATER, OP_NOT);
        break;
    case TOKEN_PLUS:
        compiler_emit_byte(OP_ADD);
        break;
    case TOKEN_MINUS:
        compiler_emit_byte(OP_SUBTRACT);
        break;
    case TOKEN_STAR:
        compiler_emit_byte(OP_MULTIPLY);
        break;
    case TOKEN_SLASH:
        compiler_emit_byte(OP_DIVIDE);
        break;
    case TOKEN_MODULO:
        compiler_emit_byte(OP_MODULO);
        break;
    case TOKEN_STAR_STAR:
        compiler_emit_byte(OP_EXPONENT);
        break;
    default:
        return;
    }
}

// Compiles a block statement to bytecode instructions
static void compiler_block()
{
    while (!compiler_check(TOKEN_RIGHT_BRACE) && !compiler_check(TOKEN_EOF))
        compiler_declaration();
    compiler_consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

// Compiles a call expression
static void compiler_call(bool canAssign)
{
    uint8_t argCount = compiler_argument_list();
    compiler_emit_bytes(OP_CALL, argCount);
}

// Checks if the next Token is of a given type
static bool compiler_check(TokenType type)
{
    return parser.current.type == type;
}

// Parses a new class declaration
static void compiler_class_declaration()
{
    compiler_consume(TOKEN_IDENTIFIER, "Expect class name.");
    Token className = parser.previous;
    uint8_t nameConstant = compiler_identifier_constant(&parser.previous);
    compiler_declare_variable();
    compiler_emit_bytes(OP_CLASS, nameConstant);
    compiler_define_variable(nameConstant);
    ClassCompiler classCompiler;
    classCompiler.hasSuperclass = false;
    classCompiler.enclosing = currentClass;
    currentClass = &classCompiler;
    if (compiler_match_token(TOKEN_DOUBLEDOT))
    {
        compiler_consume(TOKEN_IDENTIFIER, "Expect superclass name.");
        compiler_variable(false);
        if (compiler_identifiers_equal(&className, &parser.previous))
            compiler_error("A class can't inherit from itself.");
        compiler_begin_scope();
        compiler_add_local(compiler_synthetic_token("super"));
        compiler_define_variable(0);
        compiler_named_variable(className, false);
        compiler_emit_byte(OP_INHERIT);
        classCompiler.hasSuperclass = true;
    }
    compiler_named_variable(className, false);
    compiler_consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
    while (!compiler_check(TOKEN_RIGHT_BRACE) && !compiler_check(TOKEN_EOF))
        compiler_method();
    compiler_consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
    compiler_emit_byte(OP_POP);
    if (classCompiler.hasSuperclass)
        compiler_end_scope();
    currentClass = currentClass->enclosing;
}

// Consumes a Token
static void compiler_consume(TokenType type, char const *message)
{
    if (parser.current.type == type)
    {
        compiler_advance();
        return;
    }
    compiler_error_at_current(message);
}

static Chunk *compiler_current_chunk()
{
    return &current->function->chunk;
}

// Compiles a declaration stament or a normal statement
static void compiler_declaration()
{
    if (compiler_match_token(TOKEN_CLASS))
        compiler_class_declaration();
    else if (compiler_match_token(TOKEN_FUN))
        compiler_function_declaration();
    else if (compiler_match_token(TOKEN_VAR))
        compiler_var_declaration();
    else
        compiler_statement();
    if (parser.panicMode)
        compiler_synchronize();
}

// Declares a new variable
static void compiler_declare_variable()
{
    // Global scope
    if (!current->scopeDepth)
        return;

    Token * name = &parser.previous;
    for (int32_t i = current->localCount - 1; i >= 0; i--)
    {
        Local *local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth)
            break;

        if (compiler_identifiers_equal(name, &local->name))
            compiler_error("Already a variable with this name in this scope.");
    }
    compiler_add_local(*name);
}

// Defines a new Variable
static void compiler_define_variable(uint8_t global)
{
    // Local Variable
    if (current->scopeDepth > 0)
    {
        compiler_mark_initialized();
        return;
    }
    compiler_emit_bytes(OP_DEFINE_GLOBAL, global);
}

// Compiles a dot statement (get or set)
static void compiler_dot(bool canAssign)
{
    compiler_consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    uint8_t name = compiler_identifier_constant(&parser.previous);

    if (canAssign && compiler_match_token(TOKEN_EQUAL))
    {
        compiler_expression();
        compiler_emit_bytes(OP_SET_PROPERTY, name);
    }
    else if (compiler_match_token(TOKEN_LEFT_PAREN))
    {
        uint8_t argCount = compiler_argument_list();
        compiler_emit_bytes(OP_INVOKE, name);
        compiler_emit_byte(argCount);
    }
    else
    {
        compiler_emit_bytes(OP_GET_PROPERTY, name);
    }
}

// Emits a single byte as bytecode instructionn
static void compiler_emit_byte(uint8_t byte)
{
    chunk_write(compiler_current_chunk(), byte, parser.previous.line);
}

// Emits two bytes as bytecode instructions
static void compiler_emit_bytes(uint8_t byte1, uint8_t byte2)
{
    compiler_emit_byte(byte1);
    compiler_emit_byte(byte2);
}

// Creates a constant bytecode instruction
static void compiler_emit_constant(Value value)
{
    compiler_emit_bytes(OP_CONSTANT, compiler_make_constant(value));
}

/* Emits a bytecode instruction of the type jump (jump or jump-if-false)
 * and writes a placeholder to the jump offset
 * Additionally returns the offset (start address) of the then or else branch
 */
static int32_t compiler_emit_jump(uint8_t instruction)
{
    compiler_emit_byte(instruction);
    compiler_emit_byte(0xff);
    compiler_emit_byte(0xff);
    return compiler_current_chunk()->count - 2;
}

// Emits the bytecode instructions for creating a loop
static void compiler_emit_loop(int32_t loopStart)
{
    compiler_emit_byte(OP_LOOP);
    int32_t offset = compiler_current_chunk()->count - loopStart + 2;
    if (offset > (int32_t)UINT16_MAX)
        compiler_error("Loop body too large."); // There can only be 65535 lines betweem a jump instruction because we use a short
    compiler_emit_byte((offset >> 8) & 0xff);
    compiler_emit_byte(offset & 0xff);
}

// Emits a return bytecode instruction
static void compiler_emit_return()
{
    if (current->type == TYPE_INITIALIZER)
        compiler_emit_bytes(OP_GET_LOCAL, 0);
    else
        compiler_emit_byte(OP_NULL);
    compiler_emit_byte(OP_RETURN);
}

// yields a newly created function object after the compilation process finished
static ObjectFunction *compiler_end()
{
    compiler_emit_return();
    ObjectFunction * function = current->function;
#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError)
        debug_disassemble_chunk(compiler_current_chunk(), function->name != NULL ? function->name->chars : "<script>");
#endif
    current = current->enclosing;
    return function;
}

// Handles the closing of a scope
static void compiler_end_scope()
{
    current->scopeDepth--;
    /* We walk backward through the local array looking for any variables,
     * that where declared at the scope depth we just left, when we pop a scope
     */
    while (current->localCount > 0 && current->locals[current->localCount - 1].depth > current->scopeDepth)
    {
        if (current->locals[current->localCount - 1].isCaptured)
            compiler_emit_byte(OP_CLOSE_UPVALUE);
        else
            compiler_emit_byte(OP_POP);
        current->localCount--;
    }
}

// Reports an error at the previous position
static void compiler_error(char const *message)
{
    compiler_error_at(&parser.previous, message);
}

// Reports an error that was triggered by a specifiec token
static void compiler_error_at(Token *token, char const *message)
{
    if (parser.panicMode)
        return;
    parser.panicMode = true;
    fprintf(stderr, "[line %d] Error", token->line);
    if (token->type == TOKEN_EOF)
        fprintf(stderr, " at end");
    else
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

// Reports an error at the current position
static void compiler_error_at_current(char const *message)
{
    compiler_error_at(&parser.current, message);
}

// Compiles a expression
static void compiler_expression()
{
    compiler_parse_precedence(PREC_ASSIGNMENT);
}

// Compiles an expression-statement
static void compiler_expression_statement()
{
    compiler_expression();
    compiler_consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    compiler_emit_byte(OP_POP);
}

// Compiles a for-statement
static void compiler_for_statement()
{
    compiler_begin_scope();
    compiler_consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

    // Initializer clause
    if (compiler_match_token(TOKEN_VAR))
        compiler_var_declaration();
    else if(!compiler_match_token(TOKEN_SEMICOLON))
        compiler_expression_statement();

    int32_t loopStart = compiler_current_chunk()->count;
    int32_t exitJump = -1;

    // Conditional clause
    if (!compiler_match_token(TOKEN_SEMICOLON))
    {
        compiler_expression();
        compiler_consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");
        // Jump out of the loop if the condition is false.
        exitJump = compiler_emit_jump(OP_JUMP_IF_FALSE);
        compiler_emit_byte(OP_POP); // Condition.
    }

    // Increment clause
    if (!compiler_match_token(TOKEN_RIGHT_PAREN))
    {
        int32_t bodyJump = compiler_emit_jump(OP_JUMP);
        int32_t incrementStart = compiler_current_chunk()->count;
        compiler_expression();
        compiler_emit_byte(OP_POP);
        compiler_consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");
        compiler_emit_loop(loopStart);
        loopStart = incrementStart;
        compiler_patch_jump(bodyJump);
    }

    compiler_statement();
    compiler_emit_loop(loopStart);

    if (exitJump != -1)
    {
        compiler_patch_jump(exitJump);
        compiler_emit_byte(OP_POP); // Condition.
    }

    compiler_end_scope();
}

// Compiles a function declaration statement to bytecode instructions
static void compiler_function(FunctionType type)
{
    Compiler compiler;
    compiler_init(&compiler, type);
    compiler_begin_scope();

    compiler_consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!compiler_check(TOKEN_RIGHT_PAREN))
    {
        do
        {
            current->function->arity++;
            if (current->function->arity > 255)
                compiler_error_at_current("Can't have more than 255 parameters.");
            uint8_t constant = compiler_parse_variable("Expect parameter name.");
            compiler_define_variable(constant);
        } while (compiler_match_token(TOKEN_COMMA));
    }
    compiler_consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    compiler_consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    compiler_block();
    ObjectFunction * function = compiler_end();
    compiler_emit_bytes(OP_CLOSURE, compiler_make_constant(OBJECT_VAL(function)));
    for (int32_t i = 0; i < function->upvalueCount; i++)
    {
        compiler_emit_byte(compiler.upvalues[i].isLocal ? 1 : 0);
        compiler_emit_byte(compiler.upvalues[i].index);
    }
}

// Compiles a function statement and defines the function in the current environment
static void compiler_function_declaration()
{
    uint8_t global = compiler_parse_variable("Expect function name.");
    compiler_mark_initialized();
    compiler_function(TYPE_FUNCTION);
    compiler_define_variable(global);
}

// Gets the parsing rule for a specific token type
static ParseRule *compiler_get_rule(TokenType type)
{
    return &rules[type];
}

// compiles a grouping expression '(expression)'
static void compiler_grouping(bool canAssign)
{
    compiler_expression();
    compiler_consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

// Used to create a string object from an identifier token
static uint8_t compiler_identifier_constant(Token *name)
{
    return compiler_make_constant(OBJECT_VAL(object_copy_string(name->start, name->length, false)));
}

// Determines whether two identifiers are equal
static bool compiler_identifiers_equal(Token * a, Token * b)
{
    if (a->length != b->length)
        return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

// Compiles an if-statement
static void compiler_if_statement()
{
    compiler_consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    compiler_expression();
    compiler_consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
    int32_t thenJump = compiler_emit_jump(OP_JUMP_IF_FALSE);
    compiler_statement();
    int32_t elseJump = compiler_emit_jump(OP_JUMP);
    compiler_patch_jump(thenJump);
    compiler_emit_byte(OP_POP);
    if (compiler_match_token(TOKEN_ELSE))
        compiler_statement();
    // We patch that offset after the end of the else body
    compiler_patch_jump(elseJump);
}

// Initializes the compiler
static void compiler_init(Compiler * compiler, FunctionType type)
{
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->function = object_new_function();
    current = compiler;
    if (type != TYPE_SCRIPT)
        current->function->name = object_copy_string(parser.previous.start, parser.previous.length, false);
    Local *local = &current->locals[current->localCount++];
    local->depth = 0;
    local->isCaptured = false;
    if (type != TYPE_FUNCTION)
    {
        local->name.start = "this";
        local->name.length = 4;
    }
    else
    {
        local->name.start = "";
        local->name.length = 0;
    }
}

static void compiler_index_of(bool canAssign)
{
    compiler_expression();
    if(!compiler_match_token(TOKEN_RIGHT_BRACKET))
    {
        compiler_error("expected closing bracket ]");
        return;
    }
    compiler_emit_byte(OP_INDEX_OF);
}

// Compiles a boolean literal expression
static void compiler_literal(bool canAssign)
{
    switch (parser.previous.type)
    {
    case TOKEN_FALSE:
        compiler_emit_byte(OP_FALSE);
        break;
    case TOKEN_NULL:
        compiler_emit_byte(OP_NULL);
        break;
    case TOKEN_TRUE:
        compiler_emit_byte(OP_TRUE);
        break;
    default:
        return; // Unreachable.
    }
}

// Marks a variable that already has been declared as initialized
static void compiler_mark_initialized()
{
    if (current->scopeDepth == 0)
        return;
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}

// Emits a constant bytecode instruction with the value that was passed as an argument up opon the function call
static uint8_t compiler_make_constant(Value value)
{
    int32_t constant = chunk_add_constant(compiler_current_chunk(), value);
    if (constant > (int32_t)UINT8_MAX)
    {
        compiler_error("Too many constants in one chunk.");
        return 0;
    }
    return (uint8_t)constant;
}

// Determines weather the next Token is from the specified TokenTypes and advances a position further, if that is the case
static bool compiler_match_token(TokenType type)
{
    if (!compiler_check(type))
        return false;
    compiler_advance();
    return true;
}

// Compiles a method declaration
static void compiler_method()
{
    compiler_consume(TOKEN_IDENTIFIER, "Expect method name.");
    uint8_t constant = compiler_identifier_constant(&parser.previous);
    FunctionType type = TYPE_METHOD;
    if (parser.previous.length == 4 && memcmp(parser.previous.start, "init", 4) == 0)
        type = TYPE_INITIALIZER;
    compiler_function(type);
    compiler_emit_bytes(OP_METHOD, constant);
}

// Handles getting and setting a variable (locals, globals and upvalues)
static void compiler_named_variable(Token name, bool canAssign)
{
    uint8_t getOp, setOp;
    int32_t arg = compiler_resolve_local(current, &name);
    if (arg != -1)
    {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    }
    else if ((arg = compiler_resolve_upvalue(current, &name)) != -1)
    {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    }
    else
    {
        arg = compiler_identifier_constant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }
    if (canAssign && compiler_match_token(TOKEN_EQUAL))
    {
        compiler_expression();
        compiler_emit_bytes(setOp, (uint8_t)arg);
    }
    else if (canAssign && compiler_match_token(TOKEN_PLUS_EQUAL))
        compiler_nondirect_assignment(OP_ADD, getOp, setOp, arg);
    else if (canAssign && compiler_match_token(TOKEN_MINUS_EQUAL))
        compiler_nondirect_assignment(OP_SUBTRACT, getOp, setOp, arg);
    else if (canAssign && compiler_match_token(TOKEN_STAR_EQUAL))
        compiler_nondirect_assignment(OP_MULTIPLY, getOp, setOp, arg);
    else if (canAssign && compiler_match_token(TOKEN_SLASH_EQUAL))
        compiler_nondirect_assignment(OP_DIVIDE, getOp, setOp, arg);
    else if (canAssign && compiler_match_token(TOKEN_MODULO_EQUAL))
        compiler_nondirect_assignment(OP_MODULO, getOp, setOp, arg);
    else if (canAssign && compiler_match_token(TOKEN_STAR_STAR_EQUAL))
        compiler_nondirect_assignment(OP_EXPONENT, getOp, setOp, arg);
    else
        compiler_emit_bytes(getOp, (uint8_t)arg);
}

static void compiler_nondirect_assignment(uint8_t assignmentType, uint8_t getOp, uint8_t setOp, uint8_t arg)
{
    compiler_emit_bytes(getOp, arg);
    compiler_expression();
    compiler_emit_byte(assignmentType);
    compiler_emit_bytes(setOp, arg);
}

// compiles a number literal expression
static void compiler_number(bool canAssign)
{
    double value = strtod(parser.previous.start, NULL);
    compiler_emit_constant(NUMBER_VAL(value));
}

// Handles an or-expression
static void compiler_or(bool canAssign)
{
    int32_t elseJump = compiler_emit_jump(OP_JUMP_IF_FALSE);
    int32_t endJump = compiler_emit_jump(OP_JUMP);
    compiler_patch_jump(elseJump);
    compiler_emit_byte(OP_POP);
    compiler_parse_precedence(PREC_OR);
    compiler_patch_jump(endJump);
}

// Parses the precedence of a statement to determine whether a valid assignment target is specified
static void compiler_parse_precedence(Precedence precedence)
{
    compiler_advance();
    ParseFn prefixRule = compiler_get_rule(parser.previous.type)->prefix;
    if (prefixRule == NULL)
    {
        compiler_error("Expect expression.");
        return;
    }
    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);
    while (precedence <= compiler_get_rule(parser.current.type)->precedence)
    {
        compiler_advance();
        ParseFn infixRule = compiler_get_rule(parser.previous.type)->infix;
        infixRule(canAssign);
    }
    if (canAssign && compiler_match_token(TOKEN_EQUAL))
        compiler_error("Invalid assignment target.");
}

// Parses a variable statement
static uint8_t compiler_parse_variable(char const * errorMessage)
{
    compiler_consume(TOKEN_IDENTIFIER, errorMessage);
    compiler_declare_variable();
    if (current->scopeDepth > 0)
        return 0;
    return compiler_identifier_constant(&parser.previous);
}

// Replaces the operand at the given location with the calculated jump offset
static void compiler_patch_jump(int32_t offset)
{
    // -2 to adjust for the bytecode for the jump offset itself.
    int32_t jump = compiler_current_chunk()->count - offset - 2;
    if (jump > (int32_t)UINT16_MAX)
        compiler_error("Too much code to jump over."); // More than 65,535 bytes of code
    compiler_current_chunk()->code[offset] = (jump >> 8) & 0xff;
    compiler_current_chunk()->code[offset + 1] = jump & 0xff;
}

// Compiles a print statement
static void compiler_print_statement()
{
    compiler_expression();
    compiler_consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    compiler_emit_byte(OP_PRINT);
}

// Resolves a local variable name
static int32_t compiler_resolve_local(Compiler * compiler, Token *name)
{
    for (int32_t i = compiler->localCount - 1; i >= 0; i--)
    {
        Local *local = &compiler->locals[i];
        if (compiler_identifiers_equal(name, &local->name))
        {
            if (local->depth == -1)
                compiler_error("Can't read local variable in its own initializer.");
            return i;
        }
    }
    return -1;
}

/* Looks for a local variable declared in any of the surrounding functions.
If an upvalue is found it returns an upvalue index, if not -1 is returned.*/
static int32_t compiler_resolve_upvalue(Compiler * compiler, Token *name)
{
    if (compiler->enclosing == NULL)
        return -1; // not found
    int32_t local = compiler_resolve_local(compiler->enclosing, name);
    if (local != -1)
    {
        compiler->enclosing->locals[local].isCaptured = true;
        return compiler_add_upvalue(compiler, (uint8_t)local, true);
    }
    // Resolution of a local variable failed in the current environent -> look in the enclosing environment
    int32_t upvalue = compiler_resolve_upvalue(compiler->enclosing, name);
    if (upvalue != -1)
        return compiler_add_upvalue(compiler, (uint8_t)upvalue, false);
    // Upvalue couldn't be found
    return -1;
}

// Compiles a return statement
static void compiler_return_statement()
{
    if (current->type == TYPE_SCRIPT)
        compiler_error("Can't return from top-level code.");
    if (compiler_match_token(TOKEN_SEMICOLON))
        compiler_emit_return();
    else
    {
        if (current->type == TYPE_INITIALIZER)
            compiler_error("Can't return a value from an initializer.");
        compiler_expression();
        compiler_consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        compiler_emit_byte(OP_RETURN);
    }
}

// Compiles a statement
static void compiler_statement()
{
    if (compiler_match_token(TOKEN_PRINT))
        compiler_print_statement();
    else if (compiler_match_token(TOKEN_FOR))
        compiler_for_statement();
    else if (compiler_match_token(TOKEN_IF))
        compiler_if_statement();
    else if (compiler_match_token(TOKEN_RETURN))
        compiler_return_statement();
    else if (compiler_match_token(TOKEN_WHILE))
        compiler_while_statement();
    else if (compiler_match_token(TOKEN_LEFT_BRACE))
    {
        compiler_begin_scope();
        compiler_block();
        compiler_end_scope();
    }
    else
        compiler_expression_statement();
}

// compiles a string literal expression
static void compiler_string(bool canAssign)
{
    ObjectString * string = object_copy_string(parser.previous.start + 1, parser.previous.length - 2, true);
    if(string == NULL)
    {
        compiler_error("Unknown escape sequence in string");
        return;
    }
    compiler_emit_constant(OBJECT_VAL(string));
}

// Compiles a super expression
static void compiler_super(bool canAssign)
{
    if (currentClass == NULL)
        compiler_error("Can't use 'super' outside of a class.");
    else if (!currentClass->hasSuperclass)
        compiler_error("Can't use 'super' in a class with no superclass.");
    compiler_consume(TOKEN_DOT, "Expect '.' after 'super'.");
    compiler_consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
    uint8_t name = compiler_identifier_constant(&parser.previous);
    compiler_named_variable(compiler_synthetic_token("this"), false);
    if (compiler_match_token(TOKEN_LEFT_PAREN))
    {
        uint8_t argCount = compiler_argument_list();
        compiler_named_variable(compiler_synthetic_token("super"), false);
        compiler_emit_bytes(OP_SUPER_INVOKE, name);
        compiler_emit_byte(argCount);
    }
    else
    {
        compiler_named_variable(compiler_synthetic_token("super"), false);
        compiler_emit_bytes(OP_GET_SUPER, name);
    }
}

// Synchronizes the compiler after an error has occured (jumps to the next statement)
static void compiler_synchronize()
{
    parser.panicMode = false;

    while (parser.current.type != TOKEN_EOF)
    {
        if (parser.previous.type == TOKEN_SEMICOLON)
            return;
        switch (parser.current.type)
        {
        case TOKEN_CLASS:
        case TOKEN_FUN:
        case TOKEN_VAR:
        case TOKEN_FOR:
        case TOKEN_IF:
        case TOKEN_WHILE:
        case TOKEN_PRINT:
        case TOKEN_RETURN:
            return;

        default:;
        }
        compiler_advance();
    }
}

static Token compiler_synthetic_token(char const * text)
{
    Token token;
    token.start = text;
    token.length = (int)strlen(text);
    return token;
}

// Compiles a this expression
static void compiler_this(bool canAssign)
{
    if (currentClass == NULL)
    {
        compiler_error("Can't use 'this' outside of a class.");
        return;
    }

    compiler_variable(false);
}

// Compiles a unary expression
static void compiler_unary(bool canAssign)
{
    TokenType operatorType = parser.previous.type;
    // Compile the operand.
    compiler_parse_precedence(PREC_UNARY);
    // Emit the operator instruction.
    switch (operatorType)
    {
    case TOKEN_BANG:
        compiler_emit_byte(OP_NOT);
        break;
    case TOKEN_MINUS:
        compiler_emit_byte(OP_NEGATE);
        break;
    default:
        return; // Unreachable.
    }
}

// Compiles a variable declaration
static void compiler_var_declaration()
{
    uint8_t global = compiler_parse_variable("Expect variable name.");
    if (compiler_match_token(TOKEN_EQUAL))
        compiler_expression(); // Variable was initialzed
    else
        compiler_emit_byte(OP_NULL); // Variable was not initialzed
    compiler_consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
    compiler_define_variable(global);
}

/*Compiles a variable
* Get Global variable or
* Set Global variable or
* Get Local variable or
Set Local variable*/
static void compiler_variable(bool canAssign)
{
    compiler_named_variable(parser.previous, canAssign);
}

// Compiles a while statement
static void compiler_while_statement()
{
    int32_t loopStart = compiler_current_chunk()->count;
    compiler_consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    compiler_expression();
    compiler_consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
    int32_t exitJump = compiler_emit_jump(OP_JUMP_IF_FALSE);
    compiler_emit_byte(OP_POP);
    compiler_statement();
    compiler_emit_loop(loopStart);
    compiler_patch_jump(exitJump);
    compiler_emit_byte(OP_POP);
}