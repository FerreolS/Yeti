/*
 * yeti_hash.c -
 *
 * Implement hash table objects in Yorick.
 *
 *-----------------------------------------------------------------------------
 *
 * Copyright (C) 1996-2010 Eric Thiébaut <thiebaut@obs.univ-lyon1.fr>
 *
 * This software is governed by the CeCILL-C license under French law and
 * abiding by the rules of distribution of free software.  You can use, modify
 * and/or redistribute the software under the terms of the CeCILL-C license as
 * circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty and the software's author, the holder of the
 * economic rights, and the successive licensors have only limited liability.
 *
 * In this respect, the user's attention is drawn to the risks associated with
 * loading, using, modifying and/or developing or reproducing the software by
 * the user in light of its specific status of free software, that may mean
 * that it is complicated to manipulate, and that also therefore means that it
 * is reserved for developers and experienced professionals having in-depth
 * computer knowledge. Users are therefore encouraged to load and test the
 * software's suitability as regards their requirements in conditions enabling
 * the security of their systems and/or data to be ensured and, more
 * generally, to use and operate it in the same conditions as regards
 * security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL-C license and that you accept its terms.
 *
 *-----------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "yeti.h"
#include "yio.h"

#undef H_DEBUG

/*---------------------------------------------------------------------------*/
/* DEFINITIONS FOR STRING HASH TABLES */

/* Some macros to adapt implementation. */
#define h_error(MSG)     YError(MSG)
#define h_malloc(SIZE)   p_malloc(SIZE)
#define h_free(ADDR)     p_free(ADDR)

#define OFFSET(type, member) ((char *)&((type *)0)->member - (char *)0)

typedef unsigned int h_uint_t;
typedef struct h_table h_table_t;
typedef struct h_entry h_entry_t;

struct h_table {
  int references;         /* reference counter */
  Operations *ops;        /* virtual function table */
  long        eval;       /* index to eval method (-1L if none) */
  h_uint_t    number;     /* number of entries */
  h_uint_t    size;       /* number of elements in bucket */
  h_uint_t    new_size;   /* if > size, indicates rehash is needed */
  h_entry_t **bucket;     /* dynamically malloc'ed bucket of entries */
};

struct h_entry {
  h_entry_t  *next;      /* next entry or NULL */
  OpTable    *sym_ops;   /* client data value = Yorick's symbol */
  SymbolValue sym_value;
  h_uint_t    hash;      /* hashed key */
  char        name[1];   /* entry name, actual size is large enough for
                            whole string name to fit (MUST BE LAST MEMBER) */
};

/*
 * Tests about the hashing method:
 * ---------------------------------------------------------------------------
 * Hashing code         Cost(*)  Histogram of bucket occupation
 * ---------------------------------------------------------------------------
 * HASH+=(HASH<<1)+BYTE   1.38   [1386,545,100,17]
 * HASH+=(HASH<<2)+BYTE   1.42   [1399,522,107,20]
 * HASH+=(HASH<<3)+BYTE   1.43   [1404,511,116,15, 2]
 * HASH =(HASH<<1)^BYTE   1.81   [1434,481, 99,31, 2, 0,0,0,0,0,0,0,0,0,0,0,1]
 * HASH =(HASH<<2)^BYTE   2.09   [1489,401,112,31, 9, 4,1,0,0,0,0,0,0,0,0,0,1]
 * HASH =(HASH<<3)^BYTE   2.82   [1575,310, 95,28,19,10,4,3,2,1,0,0,0,0,0,0,1]
 * ---------------------------------------------------------------------------
 * (*) cost = mean # of tests to localize an item
 * TCL randomize method is:     HASH += (HASH<<3) + BYTE
 * Yorick randomize method is:  HASH  = (HASH<<1) ^ BYTE
 */

/* Piece of code to randomize a string.  HASH, LEN, BYTE and NAME must be
   variables.  HASH, LEN, BYTE must be unsigned integers (h_uint_t) and NAME
   must be an array of unsigned characters (bytes). */
#define H_HASH(HASH, LEN, NAME, BYTE)                                   \
  do {                                                                  \
    const unsigned char * __temp__ = (const unsigned char *)NAME;       \
    for (HASH = LEN = 0; (BYTE = __temp__[LEN]); ++LEN) {               \
      HASH += (HASH<<3) + BYTE;                                         \
    }                                                                   \
  } while (0)

/* Use this macro to check if hash table ENTRY match string NAME.
   LEN is the length of NAME and HASH the hash value computed from NAME. */
#define H_MATCH(ENTRY, HASH, NAME, LEN) \
  ((ENTRY)->hash == HASH && ! strncmp(NAME, (ENTRY)->name, LEN))


extern h_table_t *h_new(h_uint_t number);
/*----- Create a new empty hash table with at least NUMBER slots
        pre-allocated (rounded up to a power of 2). */

extern void h_delete(h_table_t *table);
/*----- Destroy hash table TABLE and its contents. */

extern h_entry_t *h_find(h_table_t *table, const char *name);
/*----- Returns the address of the entry in hash table TABLE that match NAME.
        If no entry is identified by NAME (or in case of error) NULL is
        returned. */

extern int h_remove(h_table_t *table, const char *name);
/*----- Remove entry identifed by NAME from hash table TABLE.  Return value
        is: 0 if no entry in TABLE match NAME, 1 if and entry matching NAME
        was found and unreferenced, -1 in case of error. */

extern int h_insert(h_table_t *table, const char *name, Symbol *sym);
/*----- Insert entry identifed by NAME with contents SYM in hash table
        TABLE.  Return value is: 0 if no former entry in TABLE matched NAME
        (hence a new entry was created); 1 if a former entry in TABLE matched
        NAME (which was properly unreferenced); -1 in case of error. */

/*---------------------------------------------------------------------------*/
/* PRIVATE ROUTINES */

extern BuiltIn Y_is_hash;
extern BuiltIn Y_h_new, Y_h_get, Y_h_set, Y_h_has, Y_h_pop, Y_h_stat;
extern BuiltIn Y_h_debug, Y_h_keys, Y_h_first, Y_h_next;

static h_table_t *get_table(Symbol *stack);
/*----- Returns hash table stored by symbol STACK.  STACK get replaced by
        the referenced object if it is a reference symbol. */

static void set_members(h_table_t *obj, Symbol *stack, int nargs);
/*----- Parse arguments STACK[0]..STACK[NARGS-1] as key-value pairs to
        store in hash table OBJ. */

static int get_table_and_key(int nargs, h_table_t **table,
                            const char **keystr);

static void get_member(Symbol *owner, h_table_t *table, const char *name);
/*----- Replace stack symbol OWNER by the contents of entry matching NAME
        in hash TABLE (taking care of UnRef/Ref properly). */

static void rehash(h_table_t *table);
/*----- Rehash hash TABLE (taking care of interrupts). */

/*--------------------------------------------------------------------------*/
/* IMPLEMENTATION OF HASH TABLES AS OPAQUE YORICK OBJECTS */

extern PromoteOp PromXX;
extern UnaryOp ToAnyX, NegateX, ComplementX, NotX, TrueX;
extern BinaryOp AddX, SubtractX, MultiplyX, DivideX, ModuloX, PowerX;
extern BinaryOp EqualX, NotEqualX, GreaterX, GreaterEQX;
extern BinaryOp ShiftLX, ShiftRX, OrX, AndX, XorX;
extern BinaryOp AssignX, MatMultX;
extern UnaryOp EvalX, SetupX, PrintX;
static MemberOp GetMemberH;
static UnaryOp PrintH;
static void FreeH(void *addr);  /* ******* Use Unref(hash) ******* */
static void EvalH(Operand *op);

Operations hashOps = {
  &FreeH, T_OPAQUE, 0, /* promoteID = */T_STRING/* means illegal */,
  "hash_table",
  {&PromXX, &PromXX, &PromXX, &PromXX, &PromXX, &PromXX, &PromXX, &PromXX},
  &ToAnyX, &ToAnyX, &ToAnyX, &ToAnyX, &ToAnyX, &ToAnyX, &ToAnyX,
  &NegateX, &ComplementX, &NotX, &TrueX,
  &AddX, &SubtractX, &MultiplyX, &DivideX, &ModuloX, &PowerX,
  &EqualX, &NotEqualX, &GreaterX, &GreaterEQX,
  &ShiftLX, &ShiftRX, &OrX, &AndX, &XorX,
  &AssignX, &EvalH, &SetupX, &GetMemberH, &MatMultX, &PrintH
};

/* FreeH is automatically called by Yorick to delete an object instance
   that is no longer referenced. */
static void FreeH(void *addr) { h_delete((h_table_t *)addr); }

/* PrintH is used by Yorick's info command. */
static void PrintH(Operand *op)
{
  h_table_t *obj = (h_table_t *)op->value;
  char line[80];
  ForceNewline();
  PrintFunc("Object of type: ");
  PrintFunc(obj->ops->typeName);
  PrintFunc(" (evaluator=");
  if (obj->eval < 0L) {
    PrintFunc("(nil)");
  } else {
    PrintFunc("\"");
    PrintFunc(globalTable.names[obj->eval]);
    PrintFunc("\"");
  }
  sprintf(line, ", references=%d, number=%u, size=%u)",
          obj->references, obj->number, obj->size);
  PrintFunc(line);
  ForceNewline();
}

/* GetMemberH implements the de-referencing '.' operator. */
static void GetMemberH(Operand *op, char *name)
{
  get_member(op->owner, (h_table_t *)op->value, name);
}

/* EvalH implements hash table used as a function or as an indexed array. */
static void EvalH(Operand *op)
{
  Symbol *s, *owner;
  h_table_t *table;
  DataBlock *old, *db;
  OpTable *ops;
  Operations *oper;
  int i, nargs, offset;

  /* Get the hash table. */
  owner = op->owner;
  table = (h_table_t *)owner->value.db;
  nargs = sp - owner; /* number of arguments */

  if (table->eval >= 0L) {
    /* this hash table implement its own eval method */
    s = &globTab[table->eval];
    while (s->ops == &referenceSym) {
      s = &globTab[s->index];
    }
    db = s->value.db; /* correctness checked below */
    if (s->ops != &dataBlockSym || db == NULL
        || ((oper = db->ops) != &functionOps &&
            oper != &builtinOps && oper != &auto_ops)) {
      YError("non-function eval method");
    }

    /* shift stack to prepend reference to eval method */
    offset = owner - spBottom; /* stack may move */
    if (CheckStack(2)) {
      owner = spBottom + offset;
      op->owner = owner;
    }
    /*** CRITICAL CODE START ***/
    {
      volatile Symbol *stack = owner;
      ++nargs; /* one more argument: the object itself */
      i = nargs;
      stack[i].ops = &intScalar; /* set safe OpTable */
      sp = (Symbol *)stack + i; /* it is now safe to grow the stack */
      while (--i >= 0) {
        ops = stack[i].ops;
        stack[i].ops = &intScalar; /* set safe OpTable */
        stack[i + 1].value = stack[i].value;
        stack[i + 1].index = stack[i].index;
        stack[i + 1].ops = ops; /* set true OpTable *after* initialization */
      }
      stack->value.db = RefNC(db); /* we already know that db != NULL */
      stack->ops = &dataBlockSym;
    }
    /*** CRITICAL CODE END ***/

    /* re-form operand and call Eval method */
    op->owner = owner; /* stack may have moved */
    op->references = nargs;   /* (see FormEvalOp in array.c) */
    op->ops = db->ops;
    op->value = db;
    op->ops->Eval(op);
    return;
  }

  /* got exactly one argument */
  if (nargs == 1 && sp->ops != NULL) {
    Operand arg;
    sp->ops->FormOperand(sp, &arg);
    if (arg.ops->typeID == T_STRING) {
      if (arg.type.dims == NULL) {
        char *name = *(char **)arg.value;
        h_entry_t *entry = h_find(table, name);
        Drop(1); /* discard key name (after using it) */
        old = (owner->ops == &dataBlockSym) ? owner->value.db : NULL;
        owner->ops = &intScalar; /* avoid clash in case of interrupts */
        if (entry != NULL) {
          if ((ops = entry->sym_ops) == &dataBlockSym) {
            db = entry->sym_value.db;
            owner->value.db = Ref(db);
          } else {
            owner->value = entry->sym_value;
          }
        } else {
          /* NULLER_DATA_BLOCK NewRange(0L, 0L, 1L, R_NULLER); */
          owner->value.db = RefNC(&nilDB);
          ops = &dataBlockSym;
        }
        Unref(old);
        owner->ops = ops;           /* change ops only AFTER value updated */
        return;
      }
    } else if (arg.ops->typeID == T_VOID) {
      Drop(2);
      PushLongValue(table->number);
      return;
    }
  }
  YError("expecting or a single hash key name or nil (integer indexing no longer supported)");
}

/*---------------------------------------------------------------------------*/
/* BUILTIN ROUTINES */

static int is_nil(Symbol *s);
static void push_string_value(const char *value);

static int is_nil(Symbol *s)
{
  while (s->ops == &referenceSym) s = &globTab[s->index];
  return (s->ops == &dataBlockSym && s->value.db == &nilDB);
}

static void push_string_value(const char *value)
{
  ((Array *)PushDataBlock(NewArray(&stringStruct,  NULL)))->value.q[0] =
    (value ? p_strcpy((char *)value) : NULL);
}

void Y_is_hash(int nargs)
{
  Symbol *s;
  int result;
  if (nargs != 1) YError("is_hash takes exactly one argument");
  s = YETI_DEREF_SYMBOL(sp);
  if (s->ops == &dataBlockSym && s->value.db->ops == &hashOps) {
    if (((h_table_t *)s->value.db)->eval >= 0L) {
      result = 2;
    } else {
      result = 1;
    }
  } else {
    result = 0;
  }
  PushIntValue(result);
}

void Y_h_debug(int nargs)
{
  int i;
  for (i=1 ; i<=nargs ; ++i) yeti_debug_symbol(sp - nargs + i);
  Drop(nargs);
}

void Y_h_new(int nargs)
{
  h_table_t *obj;
  int initial_size, got_members;
  const int min_size = 16;
  Symbol *stack = sp - nargs + 1; /* first argument (we know that the stack
                                     will NOT be moved) */
  if (nargs == 0 || (nargs == 1 && is_nil(sp))) {
    got_members = 0;
    initial_size = 0;
  } else {
    got_members = 1;
    initial_size = nargs/2;
  }
  if (initial_size < min_size) initial_size = min_size;
  obj = h_new(initial_size);
  PushDataBlock(obj);
  if (got_members) set_members(obj, stack, nargs);
}

void Y_h_set(int nargs)
{
  h_table_t *table;
  if (nargs < 1 || nargs%2 != 1)
    YError("usage: h_set,table,\"key\",value,... -or- h_set,table,key=value,...");
  table = get_table(sp - nargs + 1);
  if (nargs > 1) {
    set_members(table, sp - nargs + 2, nargs - 1);
    Drop(nargs-1); /* just left the target object on top of the stack */
  }
}

void Y_h_get(int nargs)
{
  /* Get hash table object and key name, then replace first argument (the
     hash table object) by entry contents. */
  h_table_t *table;
  const char *name;
  if (get_table_and_key(nargs, &table, &name)) {
    YError("usage: h_get(table, \"key\") -or- h_get(table, key=)");
  }
  Drop(nargs - 1);             /* only left hash table on top of stack */
  get_member(sp, table, name); /* replace top of stack by entry contents */
}

void Y_h_has(int nargs)
{
  int result;
  h_table_t *table;
  const char *name;
  if (get_table_and_key(nargs, &table, &name)) {
    YError("usage: h_has(table, \"key\") -or- h_has(table, key=)");
  }
  result = (h_find(table, name) != NULL);
  Drop(nargs);
  PushIntValue(result);
}

void Y_h_pop(int nargs)
{
  h_uint_t hash, len, code, index;
  h_entry_t *entry, *prev;
  h_table_t *table;
  const char *name;

  Symbol *stack = sp + 1; /* location to put new element */
  if (get_table_and_key(nargs, &table, &name)) {
    YError("usage: h_pop(table, \"key\") -or- h_pop(table, key=)");
  }

  /* *** Code more or less stolen from 'h_remove' *** */

  if (name) {
    /* Hash key. */
    H_HASH(hash, len, name, code);

    /* Find the entry. */
    prev = NULL;
    index = (hash % table->size);
    entry = table->bucket[index];
    while (entry) {
      if (H_MATCH(entry, hash, name, len)) {
        /* Delete the entry: (1) remove entry from chained list of entries in
           its bucket, (2) pop contents of entry, (3) free entry memory. */
        /*** CRITICAL CODE BEGIN ***/
        if (prev) prev->next = entry->next;
        else table->bucket[index] = entry->next;
        stack->ops   = entry->sym_ops;
        stack->value = entry->sym_value;
        h_free(entry);
        --table->number;
        sp = stack; /* sp updated AFTER new stack element finalized */
        /*** CRITICAL CODE END ***/
        return; /* entry found and popped */
      }
      prev = entry;
      entry = entry->next;
    }
  }
  PushDataBlock(RefNC(&nilDB)); /* entry not found */
}

void Y_h_number(int nargs)
{
  Symbol *s;
  long result;

  if (nargs != 1) YError("h_number takes exactly one argument");
  s = YETI_DEREF_SYMBOL(sp);
  if (s->ops != &dataBlockSym || s->value.db->ops != &hashOps) {
    YError("inexpected non-hash table argument");
  }
  result = ((h_table_t *)s->value.db)->number;
  PushLongValue(result);
}

void Y_h_keys(int nargs)
{
  h_entry_t *entry;
  h_table_t *table;
  char **result;
  h_uint_t i, j, number;
  if (nargs != 1) YError("h_keys takes exactly one argument");
  table = get_table(sp);
  number = table->number;
  if (number) {
    result = YETI_PUSH_NEW_Q(yeti_start_dimlist(number));
    j = 0;
    for (i = 0; i < table->size; ++i) {
      for (entry = table->bucket[i]; entry != NULL; entry = entry->next) {
        if (j >= number) YError("corrupted hash table");
        result[j++] = p_strcpy(entry->name);
      }
    }
  } else {
    PushDataBlock(RefNC(&nilDB));
  }
}

void Y_h_first(int nargs)
{
  h_table_t *table;
  char *name;
  h_uint_t j, n;
  h_entry_t **bucket;

  if (nargs != 1) YError("h_first takes exactly one argument");
  table = get_table(sp);
  name = NULL;
  bucket = table->bucket;
  n = table->size;
  for (j = 0; j < n; ++j) {
    if (bucket[j]) {
      name = bucket[j]->name;
      break;
    }
  }
  push_string_value(name);
}

void Y_h_next(int nargs)
{
  Operand arg;
  h_table_t *table;
  h_entry_t *entry, **bucket;
  const char *name;
  h_uint_t hash, len, code, j, n;

  if (nargs != 2) YError("h_next takes exactly two arguments");
  table = get_table(sp - 1);

  /* Get scalar string argument. */
  if (sp->ops == NULL) {
  bad_arg:
    YError("expecting a scalar string");
  }
  sp->ops->FormOperand(sp, &arg);
  if (arg.type.dims != NULL || arg.ops->typeID != T_STRING) {
    goto bad_arg;
  }
  name = *(const char **)arg.value;
  if (name == NULL) {
    /* Left nil string as result on top of stack. */
    return;
  }

  /* Hash key. */
  H_HASH(hash, len, name, code);

  /* Locate matching entry. */
  j = (hash % table->size);
  bucket = table->bucket;
  for (entry = bucket[j]; entry != NULL; entry = entry->next) {
    if (H_MATCH(entry, hash, (const char *)name, len)) {
      /* Get 'next' hash entry. */
      if (entry->next) {
        name = (const char *)entry->next->name;
      } else {
        name = (const char *)0;
        n = table->size;
        while (++j < n) {
          entry = bucket[j];
          if (entry) {
            name = (const char *)entry->name;
            break;
          }
        }
      }
      push_string_value(name);
      return;
    }
  }
  YError("hash entry not found");
}

void Y_h_stat(int nargs)
{
  Array *array;
  h_entry_t *entry, **bucket;
  h_table_t *table;
  long *result;
  h_uint_t i, number, max_count=0, sum_count=0;
  if (nargs != 1) YError("h_stat takes exactly one argument");
  table = get_table(sp);
  number = table->number;
  bucket = table->bucket;
  array = YETI_PUSH_NEW_ARRAY_L(yeti_start_dimlist(number + 1));
  result = array->value.l;
  for (i = 0; i <= number; ++i) {
    result[i] = 0L;
  }
  for (i = 0; i < table->size; ++i) {
    h_uint_t count = 0;
    for (entry = bucket[i]; entry != NULL; entry = entry->next) {
      ++count;
    }
    if (count <= number) {
      ++result[count];
    }
    if (count > max_count) {
      max_count = count;
    }
    sum_count += count;
  }
  if (sum_count != number) {
    table->number = sum_count;
    YError("corrupted hash table");
  }
}

#if YETI_MUST_DEFINE_AUTOLOAD_TYPE
typedef struct autoload_t autoload_t;
struct autoload_t {
  int references;      /* reference counter */
  Operations *ops;     /* virtual function table */
  long ifile;          /* index into table of autoload files */
  long isymbol;        /* global symtab index */
  autoload_t *next;    /* linked list for each ifile */
};
#endif /* YETI_MUST_DEFINE_AUTOLOAD_TYPE */

void Y_h_evaluator(int nargs)
{
  static long default_eval_index = -1; /* index of default eval method in
                                          globTab */
  static unsigned char type[256];      /* array of integers to check
                                          consistency of a symbol's name */
  h_table_t *table;
  char *str;
  long old_index;
  int push_result;

  /* Initialization of internals (digits must have lowest values). */
  if (default_eval_index < 0L) {
    int i;
    unsigned char value = 0;
    for (i = 0; i < 256; ++i) {
      type[i] = value;
    }
    for (i = '0'; i <= '9'; ++i) {
      type[i] = ++value;
    }
    for (i = 'A'; i <= 'Z'; ++i) {
      type[i] = ++value;
    }
    type['_'] = ++value;
    for (i = 'a'; i <= 'z'; ++i) {
      type[i] = ++value;
    }
    default_eval_index = Globalize("*hash_evaluator*", 0L);
  }

  if (nargs < 1 || nargs > 2) YError("h_evaluator takes 1 or 2 arguments");
  push_result =  ! yarg_subroutine();
  table = get_table(sp - nargs + 1);
  old_index = table->eval;

  if (nargs == 2) {
    long new_index = -1L;
    Symbol *s = sp;
    while (s->ops == &referenceSym) {
      s = &globTab[s->index];
    }
    if (s->ops == &dataBlockSym) {
      Operations *ops = s->value.db->ops;
      if (ops == &functionOps) {
        new_index = ((Function *)s->value.db)->code[0].index;
      } else if (ops == &builtinOps) {
        new_index = ((BIFunction *)s->value.db)->index;
      } else if (ops == &auto_ops) {
        new_index = ((autoload_t *)s->value.db)->isymbol;
      } else if (ops == &stringOps) {
        Array *a = (Array *)s->value.db;
        if (a->type.dims == NULL) {
          /* got a scalar string */
          unsigned char *q = (unsigned char *)a->value.q[0];
          if (q == NULL) {
            /* nil symbol's name corresponds to default value */
            new_index = default_eval_index;
          } else {
            /* symbol's name must not have a zero length, nor start with
               an invalid character nor a digit */
            if (type[q[0]] > 10) {
              int c, i = 0;
              for (;;) {
                if ((c = q[++i]) == 0) {
                  new_index = Globalize((char *)q, i);
                  break;
                }
                if (! type[c]) {
                  /* symbol's must not contain an invalid character */
                  break;
                }
              }
            }
          }
        }
      } else if (ops == &voidOps) {
        /* void symbol corresponds to default value */
        new_index = default_eval_index;
      }
    }
    if (new_index < 0L) {
      YError("evaluator must be a function or a valid symbol's name");
    }
    if (new_index == default_eval_index) {
      table->eval = -1L;
    } else {
      table->eval = new_index;
    }
  }
  if (push_result) {
    if (old_index >= 0L && old_index != default_eval_index) {
      str = globalTable.names[old_index];
    } else {
      str = (char *)0;
    }
    push_string_value(str);
  }
}

/*---------------------------------------------------------------------------*/

static void get_member(Symbol *owner, h_table_t *table, const char *name)
{
  OpTable *ops;
  h_entry_t *entry = h_find(table, name);
  DataBlock *old = (owner->ops == &dataBlockSym) ? owner->value.db : NULL;
  owner->ops = &intScalar;     /* avoid clash in case of interrupts */
  if (entry) {
    if ((ops = entry->sym_ops) == &dataBlockSym) {
      DataBlock *db = entry->sym_value.db;
      owner->value.db = Ref(db);
    } else {
      owner->value = entry->sym_value;
    }
  } else {
    owner->value.db = RefNC(&nilDB);
    ops = &dataBlockSym;
  }
  owner->ops = ops;            /* change ops only AFTER value updated */
  Unref(old);
}

/* get args from the top of the stack: first arg is hash table, second arg
   should be key name or keyword followed by third nil arg */
static int get_table_and_key(int nargs, h_table_t **table,
                             const char **keystr)
{
  Operand op;
  Symbol *s, *stack;

  stack = sp - nargs + 1;
  if (nargs == 2) {
    /* e.g.: foo(table, "key") */
    s = stack + 1; /* symbol for key */
    if (s->ops) {
      s->ops->FormOperand(s, &op);
      if (! op.type.dims && op.ops->typeID == T_STRING) {
        *table = get_table(stack);
        *keystr = *(char **)op.value;
        return 0;
      }
    }
  } else if (nargs == 3) {
    /* e.g.: foo(table, key=) */
    if (! (stack + 1)->ops && is_nil(stack + 2)) {
      *table = get_table(stack);
      *keystr = globalTable.names[(stack + 1)->index];
      return 0;
    }
  }
  return -1;
}

static h_table_t *get_table(Symbol *stack)
{
  DataBlock *db;
  Symbol *sym = (stack->ops == &referenceSym) ? &globTab[stack->index] : stack;
  if (sym->ops != &dataBlockSym || sym->value.db->ops != &hashOps)
    YError("expected hash table object");
  db = sym->value.db;
  if (sym != stack) {
    /* Replace reference onto the stack (equivalent to the statement
       ReplaceRef(s); see ydata.c for actual code of this routine). */
    stack->value.db = Ref(db);
    stack->ops = &dataBlockSym;     /* change ops only AFTER value updated */
  }
  return (h_table_t *)db;
}

static void set_members(h_table_t *table, Symbol *stack, int nargs)
{
  Operand op;
  int i;
  const char *name;

  if (nargs%2 != 0) YError("last key has no value");
  for (i = 0; i < nargs; i += 2, stack += 2) {
    /* Get key name. */
    if (stack->ops) {
      stack->ops->FormOperand(stack, &op);
      if (! op.type.dims && op.ops == &stringOps) {
        name = *(char **)op.value;
      } else {
        name = NULL;
      }
    } else {
      name = globalTable.names[stack->index];
    }
    if (! name) {
      YError("bad key, expecting a non-nil scalar string name or a keyword");
    }

    /* Replace value. */
    h_insert(table, name, stack + 1);
  }
}

/*--------------------------------------------------------------------------*/
/* The following code implement management of hash tables with string keys
   and aimed at the storage of Yorick DataBlock.  The randomization
   algorithm is taken from Tcl (which is 25-30% more efficient than
   Yorick's algorithm). */

h_table_t *h_new(h_uint_t number)
{
  h_uint_t nbytes, size = 1;
  h_table_t *table;

  /* Member SIZE of a hash table is always a power of 2, greater or
     equal 2*NUMBER (twice the number of entries in the table). */
  while (size < number) {
    size <<= 1;
  }
  size <<= 1;
  nbytes = size*sizeof(h_entry_t *);
  table = h_malloc(sizeof(h_table_t));
  if (table == NULL) {
  enomem:
    h_error("insufficient memory for new hash table");
    return NULL;
  }
  table->bucket = h_malloc(nbytes);
  if (table->bucket == NULL) {
    h_free(table);
    goto enomem;
  }
  memset(table->bucket, 0, nbytes);
  table->references = 0;
  table->ops = &hashOps;
  table->eval = -1L;
  table->number = 0;
  table->size = size;
  table->new_size = size;
  return table;
}

void h_delete(h_table_t *table)
{
  h_uint_t i, size;;
  h_entry_t *entry, **bucket;

  if (table != NULL) {
    if (table->new_size > table->size) {
      rehash(table);
    }
    size = table->size;
    bucket = table->bucket;
    for (i = 0; i < size; ++i) {
      entry = bucket[i];
      while (entry) {
        void *addr = entry;
        if (entry->sym_ops == &dataBlockSym) {
          DataBlock *db = entry->sym_value.db;
          Unref(db);
        }
        entry = entry->next;
        h_free(addr);
      }
    }
    h_free(bucket);
    h_free(table);
  }
}

h_entry_t *h_find(h_table_t *table, const char *name)
{
  h_uint_t hash, len, code;
  h_entry_t *entry;

  /* Check key string and compute hash value. */
  if (name == NULL) return NULL; /* not found */
  H_HASH(hash, len, name, code);

  /* Ensure consistency of the bucket. */
  if (table->new_size > table->size) {
    rehash(table);
  }

  /* Locate matching entry. */
  for (entry = table->bucket[hash % table->size];
       entry != NULL; entry = entry->next) {
    if (H_MATCH(entry, hash, name, len)) return entry;
  }

  /* Not found. */
  return NULL;
}

int h_remove(h_table_t *table, const char *name)
{
  h_uint_t hash, len, code, index;
  h_entry_t *entry, *prev;

  /* Check key string and compute hash value. */
  if (name == NULL) return 0; /* not found */
  H_HASH(hash, len, name, code);

  /* Ensure consistency of the bucket. */
  if (table->new_size > table->size) {
    rehash(table);
  }

  /* Find the entry. */
  prev = NULL;
  index = hash % table->size;
  entry = table->bucket[index];
  while (entry != NULL) {
    if (H_MATCH(entry, hash, name, len)) {
      /* Delete the entry: (1) remove entry from chained list of entries in
         its bucket, (2) unreference contents of entry, (3) free entry
         memory. */
      /*** CRITICAL CODE BEGIN ***/
      if (prev != NULL) {
        prev->next = entry->next;
      } else {
        table->bucket[index] = entry->next;
      }
      if (entry->sym_ops == &dataBlockSym) {
        DataBlock *db = entry->sym_value.db;
        Unref(db);
      }
      h_free(entry);
      --table->number;
      /*** CRITICAL CODE END ***/
      return 1; /* entry found and deleted */
    }
    prev = entry;
    entry = entry->next;
  }
  return 0; /* not found */
}

int h_insert(h_table_t *table, const char *name, Symbol *sym)
{
  h_uint_t hash, len, code, index;
  h_entry_t *entry;
  DataBlock *db;

  /* Check key string. */
  if (name == NULL) {
    h_error("invalid nil key name");
    return -1; /* error */
  }

  /* Hash key. */
  H_HASH(hash, len, name, code);

  /* Ensure consistency of the bucket. */
  if (table->new_size > table->size) {
    rehash(table);
  }

  /* Prepare symbol for storage. */
  if (sym->ops == &referenceSym) {
    /* We do not need to call ReplaceRef because the referenced symbol will
       be properly inserted into the hash table and the stack symbol will
       be left unchanged. */
    sym = &globTab[sym->index];
  }
  if (sym->ops == &dataBlockSym && sym->value.db->ops == &lvalueOps) {
    /* Symbol is an LValue, e.g. part of an array, we fetch (make a private
       copy of) the data to release the link on the total array. */
    FetchLValue(sym->value.db, sym);
  }

  /* Replace contents of the entry with same key name if it already exists. */
  for (entry = table->bucket[hash % table->size];
       entry != NULL; entry = entry->next) {
    if (H_MATCH(entry, hash, name, len)) {
      /*** CRITICAL CODE BEGIN ***/
      db = (entry->sym_ops == &dataBlockSym) ? entry->sym_value.db : NULL;
      entry->sym_ops = &intScalar; /* avoid clash in case of interrupts */
      Unref(db);
      if (sym->ops == &dataBlockSym) {
        db = sym->value.db;
        entry->sym_value.db = Ref(db);
      } else {
        entry->sym_value = sym->value;
      }
      entry->sym_ops = sym->ops;   /* change ops only AFTER value updated */
      /*** CRITICAL CODE END ***/
      return 1; /* old entry replaced */
    }
  }

  /* Must create a new entry. */
  if (((table->number + 1)<<1) > table->size) {
    /* Must grow hash table bucket, i.e. "re-hash".  This is done in such a way
       that the bucket is always consistent. This is needed to be robust in
       case of interrupts (at most one entry could be lost in this case). */
    h_entry_t **old_bucket, **new_bucket;
    h_uint_t size;
    size_t nbytes;

    size = table->size;
    nbytes = size*sizeof(h_entry_t *);
    old_bucket = table->bucket;
    new_bucket = h_malloc(2*nbytes);
    if (new_bucket == NULL) {
    not_enough_memory:
      h_error("insufficient memory to store new hash entry");
      return -1;
    }
    memcpy(new_bucket, old_bucket, nbytes);
    memset((char *)new_bucket + nbytes, 0, nbytes);
    /*** CRITICAL CODE BEGIN ***/
    table->bucket = new_bucket;
    table->new_size = 2*table->size;
    h_free(old_bucket);
    /*** CRITICAL CODE END ***/
    rehash(table);
  }

  /* Create new entry. */
  entry = h_malloc(OFFSET(h_entry_t, name) + 1 + len);
  if (entry == NULL) goto not_enough_memory;
  memcpy(entry->name, name, len+1);
  entry->hash = hash;
  if (sym->ops == &dataBlockSym) {
    db = sym->value.db;
    entry->sym_value.db = Ref(db);
  } else {
    entry->sym_value = sym->value;
  }
  entry->sym_ops = sym->ops;

  /* Insert new entry. */
  index = hash % table->size;
  /*** CRITICAL CODE BEGIN ***/
  entry->next = table->bucket[index];
  table->bucket[index] = entry;
  ++table->number;
  /*** CRITICAL CODE END ***/
  return 0; /* a new entry was created */
}

/* This function rehash a recently grown hash table.  The complications come
   from the needs to be robust with respet to interruptions so that the task
   can be interrupted at (almost) any time and resumed later with a minimun
   risk to loose entries. */
static void rehash(h_table_t *table)
{
  h_entry_t **bucket, *prev, *entry;
  h_uint_t i, j, new_size, old_size;

  if (table->new_size > table->size) {
    bucket = table->bucket;
    old_size = table->size;
    new_size = table->new_size;
    for (i = 0; i < old_size; ++i) {
      prev = NULL;
      entry = bucket[i];
      while (entry != NULL) {
        /* Compute index of the entry in the new bucket. */
        j = entry->hash % new_size;
        if (j == i) {
          /* No change in entry location, just move to next entry in bucket. */
          prev = entry;
          entry = entry->next;
        } else {
          /*** CRITICAL CODE BEGIN ***/
          /* Remove entry from its bucket. */
          if (prev == NULL) {
            bucket[i] = entry->next;
          } else {
            prev->next = entry->next;
          }
          /* Insert entry in its new bucket. */
          entry->next = bucket[j];
          bucket[j] = entry;
          /*** CRITICAL CODE END ***/
          /* Move to next entry in former bucket. */
          entry = ((prev == NULL) ? bucket[i] : prev->next);
        }
      }
    }
    table->size = new_size;
  }
}
