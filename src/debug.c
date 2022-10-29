#include "debug.h"

#include <stdio.h>

#include "object.h"
#include "value.h"

static int32_t debug_byte_instruction(char const *, chunk_t *, int32_t);
static int32_t debug_constant_instruction(char const *, chunk_t *, int32_t);
static int debug_invoke_instruction(char const *, chunk_t *, int);
static int32_t debug_jump_instruction(char const *, int32_t, chunk_t *, int32_t);
static int32_t debug_simple_instruction(char const *, int32_t);

// Dissasembles a chunk of bytecode instructions
void debug_disassemble_chunk(chunk_t * chunk, char const *name)
{
  printf("== %s ==\n", name);
  for (int32_t offset = 0; offset < chunk->count;)
    offset = debug_disassemble_instruction(chunk, offset);
}

// Dissasembles a single instruction
int32_t debug_disassemble_instruction(chunk_t * chunk, int32_t offset)
{
  printf("%04X ", offset);
  if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1])
    printf("   | ");
  else
    printf("%4d ", chunk->lines[offset]);
  // Instruction specific behaviour
  uint8_t instruction = chunk->code[offset];
  // Switch statement could be converted to a computed goto https://eli.thegreenplace.net/2012/07/12/computed-goto-for-efficient-dispatch-tables for efficiency
  printf(" OP_%02X: ", instruction);
  switch (instruction)
  {
  case OP_ADD:
    return debug_simple_instruction("ADD", offset);
  case OP_CALL:
    return debug_byte_instruction("CALL", chunk, offset);
  case OP_CLASS:
    return debug_constant_instruction("CLASS", chunk, offset);
  case OP_CLOSURE:
  {
    offset++;
    uint8_t constant = chunk->code[offset++];
    printf("%-16s %04X ", "CLOSURE", constant);
    value_print(chunk->constants.values[constant]);
    printf("\n");
    object_function_t *function = AS_FUNCTION(chunk->constants.values[constant]);
    for (uint32_t j = 0; j < function->upvalueCount; j++)
    {
      int32_t isLocal = chunk->code[offset++];
      int32_t index = chunk->code[offset++];
      printf("%04X      |                     %s %d\n", offset - 2, isLocal ? "local" : "upvalue", index);
    }
    return offset;
  }
  case OP_CLOSE_UPVALUE:
    return debug_simple_instruction("CLOSE_UPVALUE", offset);
  case OP_CONSTANT:
    return debug_constant_instruction("CONSTANT", chunk, offset);
  case OP_DEFINE_GLOBAL:
    return debug_constant_instruction("DEFINE_GLOBAL", chunk, offset);
  case OP_DIVIDE:
    return debug_simple_instruction("DIVIDE", offset);
  case OP_EQUAL:
    return debug_simple_instruction("EQUAL", offset);
  case OP_EXPONENT:
    return debug_simple_instruction("EXPONENT", offset);
  case OP_FALSE:
    return debug_simple_instruction("FALSE", offset);
  case OP_GET_GLOBAL:
    return debug_constant_instruction("GET_GLOBAL", chunk, offset);
  case OP_GET_LOCAL:
    return debug_byte_instruction("GET_LOCAL", chunk, offset);
  case OP_GET_PROPERTY:
    return debug_constant_instruction("GET_PROPERTY", chunk, offset);
  case OP_GET_SUPER:
    return debug_constant_instruction("GET_SUPER", chunk, offset);
  case OP_GET_UPVALUE:
    return debug_byte_instruction("GET_UPVALUE", chunk, offset);
  case OP_GREATER:
    return debug_simple_instruction("GREATER", offset);
  case OP_INDEX_OF:
    return debug_simple_instruction("INDEX OF", offset);
  case OP_INHERIT:
    return debug_simple_instruction("INHERIT", offset);
  case OP_INVOKE:
    return debug_invoke_instruction("INVOKE", chunk, offset);    
  case OP_JUMP:
    return debug_jump_instruction("JUMP", 1, chunk, offset);
  case OP_JUMP_IF_FALSE:
    return debug_jump_instruction("JUMP_IF_FALSE", 1, chunk, offset);
  case OP_LESS:
    return debug_simple_instruction("LESS", offset);
  case OP_LOOP:
    return debug_jump_instruction("LOOP", -1, chunk, offset);
  case OP_METHOD:
    return debug_constant_instruction("METHOD", chunk, offset);
  case OP_MODULO:
    return debug_simple_instruction("MODULO", offset);
  case OP_MULTIPLY:
    return debug_simple_instruction("MULTIPLY", offset);
  case OP_NEGATE:
    return debug_simple_instruction("NEGATE", offset);
  case OP_NOT:
    return debug_simple_instruction("NOT", offset);
  case OP_NULL:
    return debug_simple_instruction("NULL", offset);
  case OP_POP:
    return debug_simple_instruction("POP", offset);
  case OP_PRINT:
    return debug_simple_instruction("PRINT", offset);
  case OP_RETURN:
    return debug_simple_instruction("RETURN", offset);
  case OP_SET_LOCAL:
    return debug_byte_instruction("SET_LOCAL", chunk, offset);
  case OP_SET_GLOBAL:
    return debug_constant_instruction("SET_GLOBAL", chunk, offset);
  case OP_SET_UPVALUE:
    return debug_byte_instruction("SET_UPVALUE", chunk, offset);
  case OP_SET_PROPERTY:
    return debug_constant_instruction("SET_PROPERTY", chunk, offset);
  case OP_SUBTRACT:
    return debug_simple_instruction("SUBTRACT", offset);
  case OP_SUPER_INVOKE:
    return debug_invoke_instruction("SUPER_INVOKE", chunk, offset);
  case OP_TRUE:
    return debug_simple_instruction("TRUE", offset);
  default:
    printf("Unknown opcode %02X\n", instruction);
    return offset + 1;
  }
}

// Shows the slot number of a local variable
static int32_t debug_byte_instruction(char const * name, chunk_t * chunk, int32_t offset)
{
  uint8_t slot = *(chunk->code + offset + 1);
  printf("%-16s %04X\n", name, slot);
  return offset + 2;
}

// Dissasembles a constant instruction - OP_CONSTANT
static int32_t debug_constant_instruction(char const * name, chunk_t * chunk, int32_t offset)
{
  uint8_t constant = chunk->code[offset + 1];
  printf("%-16s %04X '", name, constant);
  value_print(chunk->constants.values[constant]);
  printf("'\n");
  return offset + 2;
}

// Dissasembles a invoke instruction
static int debug_invoke_instruction(char const * name, chunk_t * chunk, int offset)
{
  uint8_t constant = *(chunk->code + offset + 1);
  uint8_t argCount = *(chunk->code + offset + 2);
  printf("%-16s (%d args) %04X '", name, argCount, constant);
  value_print(chunk->constants.values[constant]);
  printf("'\n");
  return offset + 3;
}

// Dissasembles a jump instruction (with a 16-bit operand)
static int32_t debug_jump_instruction(char const * name, int32_t sign, chunk_t * chunk, int32_t offset)
{
  uint16_t jump = (uint16_t)(*(chunk->code + offset + 1) << 8);
  jump |= *(chunk->code + offset + 2);
  printf("%-16s %04X -> %04X\n", name, offset, offset + 3 + sign * jump);
  return offset + 3;
}

// Dissasembles a return instruction - OP_RETURN
static int32_t debug_simple_instruction(char const * name, int32_t offset)
{
  printf("%s\n", name);
  return offset + 1;
}