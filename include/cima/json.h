#ifndef CIMA_JSON_H
#define CIMA_JSON_H

#include "cima.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================================================== */
/* a dom for json that has no schema, `serde_json::Value` and `json!` for c.                                  */
/*                                                                                                            */
/*   cima_json *doc = cima_json_parse(&arena, text, len, &err);      every node lives in the arena            */
/*   int64_t n = CIMA_JSON_OR(cima_json_at(doc, "/meta/count"), 0);   typed reads with a fallback              */
/*   cima_json *body = CIMA_JSON_OBJ("error", false, "meta", doc);    a builder, no allocation                 */
/*   char *text = cima_json_text(CILO_OF(&arena), body, nullptr);                                              */
/*                                                                                                            */
/* objects keep their key order. the lowercase syntax lives in cima/dsl.h.                                    */
/* ========================================================================================================== */

typedef enum cima_json_type : uint8_t {
  CIMA_JSON_NULL,
  CIMA_JSON_BOOL,
  CIMA_JSON_INT,
  // integers past INT64_MAX.
  CIMA_JSON_UINT,
  CIMA_JSON_DOUBLE,
  CIMA_JSON_STRING,
  CIMA_JSON_ARRAY,
  CIMA_JSON_OBJECT,
  // json text, written as is.
  CIMA_JSON_RAW,
  // a c value, written through its cima schema.
  CIMA_JSON_TYPED,
} cima_json_type;

typedef struct cima_json_member cima_json_member;

typedef struct cima_json_typed {
  const cima_schema *schema;
  const void *value;
} cima_json_typed;

// 16 bytes. a zeroed node is null.
struct cima_json {
  cima_json_type type;
  uint8_t flags;
  // bytes of a string or raw text, items of an array, members of an object.
  uint32_t len;
  union {
    bool b;
    int64_t i;
    uint64_t u;
    double d;
    // nul terminated, utf-8. it may hold a \u0000 of its own, `len` is the truth.
    const char *str;
    cima_json *items;
    cima_json_member *members;
    const cima_json_typed *typed;
  };
};

struct cima_json_member {
  const char *key;
  // zero means `strlen(key)`.
  uint32_t key_len;
  uint8_t flags;
  cima_json value;
};

// spans shaped like every list, so CIMA_EACH walks them.
typedef struct cima_json_items {
  cima_json *items;
  size_t len;
} cima_json_items;
typedef struct cima_json_members {
  cima_json_member *items;
  size_t len;
} cima_json_members;

// the node belongs to a builder: its children and strings are not in an arena yet. functions that store a node
// copy what carries this flag, so a builder value never outlives its block by accident.
#define CIMA_JSON_TEMP ((uint8_t)1)
// children storage owned by an arena with a power of two capacity, grown in place.
#define CIMA_JSON__GROW ((uint8_t)2)
// a string or key needs no escaping.
#define CIMA_JSON__CLEAN ((uint8_t)4)
// a string or key is valid utf-8, it may still need escapes.
#define CIMA_JSON__UTF8 ((uint8_t)8)
// 32 bytes past the end of a string or key can be read, it lives in the text of a parse.
#define CIMA_JSON__PADDED ((uint8_t)16)

/* parsing. */

typedef struct cima_json_error {
  // byte offset of the problem.
  size_t offset;
  const char *message;
} cima_json_error;

// strict rfc 8259 with utf-8 validation, nesting stops at 512. null on failure, `err` may be null.
cima_json *cima_json_parse(cilo_arena *a, const char *text, size_t len, cima_json_error *err);
// whether `text` is one valid json value.
bool cima_json_valid(const char *text, size_t len);

/* reading. a null node reads as missing, so lookups chain without checks. */

static inline cima_json_type cima_json_type_of(const cima_json *v) { return v ? v->type : CIMA_JSON_NULL; }
static inline bool cima_json_is_null(const cima_json *v) { return v && v->type == CIMA_JSON_NULL; }
static inline bool cima_json_is_number(const cima_json *v) {
  return v && v->type >= CIMA_JSON_INT && v->type <= CIMA_JSON_DOUBLE;
}
static inline size_t cima_json_len(const cima_json *v) {
  return v && (v->type == CIMA_JSON_ARRAY || v->type == CIMA_JSON_OBJECT || v->type == CIMA_JSON_STRING) ? v->len
                                                                                                         : 0;
}
static inline cima_json_items cima_json_array(const cima_json *v) {
  return v && v->type == CIMA_JSON_ARRAY ? (cima_json_items){v->items, v->len} : (cima_json_items){};
}
static inline cima_json_members cima_json_object(const cima_json *v) {
  return v && v->type == CIMA_JSON_OBJECT ? (cima_json_members){v->members, v->len} : (cima_json_members){};
}
static inline size_t cima_json_key_len(const cima_json_member *m) { return m->key_len ? m->key_len : strlen(m->key); }

[[gnu::pure]] cima_json *cima_json_get(const cima_json *object, const char *key);
[[gnu::pure]] cima_json *cima_json_get_n(const cima_json *object, const char *key, size_t len);
[[gnu::pure]] cima_json *cima_json_index(const cima_json *array, size_t index);
// rfc 6901: "/files/0/name", "" is the root.
[[gnu::pure]] cima_json *cima_json_at(const cima_json *root, const char *pointer);

// the value when it has the type, else the fallback. integers read from doubles that hold one exactly.
[[gnu::pure]] bool cima_json_as_bool(const cima_json *v, bool fallback);
[[gnu::pure]] int64_t cima_json_as_int(const cima_json *v, int64_t fallback);
[[gnu::pure]] uint64_t cima_json_as_uint(const cima_json *v, uint64_t fallback);
[[gnu::pure]] double cima_json_as_double(const cima_json *v, double fallback);
[[gnu::pure]] const char *cima_json_as_string(const cima_json *v, const char *fallback);

// the fallback's type picks the reader: `CIMA_JSON_OR(v, 25)`, `CIMA_JSON_OR(v, "guest")`, `CIMA_JSON_OR(v, false)`.
// `CIMA_JSON_OR(v, nullptr)` is the string or null.
#define CIMA_JSON_OR(v, fallback)                                                                                \
  _Generic((fallback),                                                                                           \
      typeof(nullptr): cima_json__string_or_null,                                                                \
      bool: cima_json_as_bool,                                                                                   \
      float: cima_json_as_double,                                                                                \
      double: cima_json_as_double,                                                                               \
      unsigned long: cima_json_as_uint,                                                                          \
      unsigned long long: cima_json_as_uint,                                                                     \
      char *: cima_json_as_string,                                                                               \
      const char *: cima_json_as_string,                                                                         \
      default: cima_json_as_int)(v, fallback)
// the member `key` of `v` read the same way: `CIMA_JSON_GET_OR(user, "name", nullptr)`.
#define CIMA_JSON_GET_OR(v, key, fallback) CIMA_JSON_OR(cima_json_get(v, key), fallback)

static inline const char *cima_json__string_or_null(const cima_json *v, typeof(nullptr)) {
  return cima_json_as_string(v, nullptr);
}

// zod's z.coerce: numbers and booleans from strings too, as apis that write big numbers as text send them. a
// number reads as a boolean that is not zero, "true", "1", "on" and "yes" as true and their opposites as false.
// integers read from doubles that hold one exactly, and from strings holding a json number that is one.
int64_t cima_json_coerce_int(const cima_json *v, int64_t fallback);
uint64_t cima_json_coerce_uint(const cima_json *v, uint64_t fallback);
double cima_json_coerce_double(const cima_json *v, double fallback);
[[gnu::pure]] bool cima_json_coerce_bool(const cima_json *v, bool fallback);

#define CIMA_JSON_COERCE(v, fallback)                                                                            \
  _Generic((fallback),                                                                                           \
      bool: cima_json_coerce_bool,                                                                               \
      float: cima_json_coerce_double,                                                                            \
      double: cima_json_coerce_double,                                                                           \
      unsigned long: cima_json_coerce_uint,                                                                      \
      unsigned long long: cima_json_coerce_uint,                                                                 \
      default: cima_json_coerce_int)(v, fallback)
#define CIMA_JSON_GET_COERCE(v, key, fallback) CIMA_JSON_COERCE(cima_json_get(v, key), fallback)

/* building. CIMA_JSON_OBJ and CIMA_JSON_ARR need no allocator: the nodes are compound literals living until    */
/* the end of the enclosing block, which is all a response needs. cima_json_clone keeps one for longer.         */
/*                                                                                                            */
/*   CIMA_JSON_OBJ("error", false, "status", 200, "meta", CIMA_JSON_TYPED(User, &user), "i18n", nullptr)       */
/*                                                                                                            */
/* values are picked by _Generic: bool, any integer, float and double, strings, nullptr, cima_json * nodes      */
/* nested builders included, CIMA_JSON_RAW text and CIMA_JSON_TYPED values. a null string is written as null.   */
/* a comparison is an int in c, write `(bool)(a < b)` for a boolean.                                          */
/* ========================================================================================================== */

static inline cima_json cima_json__null(typeof(nullptr)) { return (cima_json){}; }
static inline cima_json cima_json__bool(bool b) { return (cima_json){.type = CIMA_JSON_BOOL, .b = b}; }
static inline cima_json cima_json__int(int64_t i) { return (cima_json){.type = CIMA_JSON_INT, .i = i}; }
static inline cima_json cima_json__uint(uint64_t u) {
  if (u > INT64_MAX) return (cima_json){.type = CIMA_JSON_UINT, .u = u};
  return (cima_json){.type = CIMA_JSON_INT, .i = (int64_t)u};
}
static inline cima_json cima_json__double(double d) { return (cima_json){.type = CIMA_JSON_DOUBLE, .d = d}; }
cima_json cima_json__string(const char *s);
static inline cima_json cima_json__node(const cima_json *v) { return v ? *v : (cima_json){}; }

#define CIMA_JSON_V(x)                                                                                           \
  _Generic((x),                                                                                                  \
      typeof(nullptr): cima_json__null,                                                                          \
      bool: cima_json__bool,                                                                                     \
      char: cima_json__int,                                                                                      \
      signed char: cima_json__int,                                                                               \
      short: cima_json__int,                                                                                     \
      int: cima_json__int,                                                                                       \
      long: cima_json__int,                                                                                      \
      long long: cima_json__int,                                                                                 \
      unsigned char: cima_json__uint,                                                                            \
      unsigned short: cima_json__uint,                                                                           \
      unsigned int: cima_json__uint,                                                                             \
      unsigned long: cima_json__uint,                                                                            \
      unsigned long long: cima_json__uint,                                                                       \
      float: cima_json__double,                                                                                  \
      double: cima_json__double,                                                                                 \
      long double: cima_json__double,                                                                            \
      char *: cima_json__string,                                                                                 \
      const char *: cima_json__string,                                                                           \
      cima_json *: cima_json__node,                                                                              \
      const cima_json *: cima_json__node)(x)

// any value above as a cima_json *, valid until the end of the enclosing block.
#define CIMA_JSON_OF(x) ((cima_json[]){CIMA_JSON_V(x)})

[[gnu::returns_nonnull]] cima_json *cima_json__obj(cima_json *node, cima_json_member *members);
[[gnu::returns_nonnull]] cima_json *cima_json__arr(cima_json *node, cima_json *items);
[[gnu::returns_nonnull]] cima_json *cima_json__raw(cima_json *node, const char *json);

// `CIMA_JSON_OBJ("key", value, ...)` and `CIMA_JSON_ARR(value, ...)`. a trailing comma is fine.
#define CIMA_JSON_OBJ(...)                                                                                       \
  cima_json__obj(&(cima_json){}, (cima_json_member[]){CIMA_PP_FOR_EACH_PAIR(CIMA__JSON_KV, __VA_ARGS__){}})
#define CIMA__JSON_KV(k, v) {.key = (k), .value = CIMA_JSON_V(v)},
#define CIMA_JSON_ARR(...)                                                                                       \
  cima_json__arr(&(cima_json){}, (cima_json[]){CIMA_PP_FOR_EACH(CIMA__JSON_ITEM, __VA_ARGS__){.flags = 0xff}})
#define CIMA__JSON_ITEM(...) __VA_OPT__(CIMA_JSON_V(__VA_ARGS__), )
// json text spliced as is, it must be valid.
#define CIMA_JSON_RAW(json) cima_json__raw(&(cima_json){}, json)
// a typed value written through its schema: `CIMA_JSON_TYPED(User, &user)`.
#define CIMA_JSON_TYPED(node, ptr)                                                                               \
  (&(cima_json){.type = CIMA_JSON_TYPED,                                                                         \
                .flags = CIMA_JSON_TEMP,                                                                         \
                .typed = &(const cima_json_typed){CIMA_SCHEMA(node), CIMA_CHECKED(node, ptr)}})

/* keeping and changing. every function storing a node copies its builder parts into the arena first. the     */
/* node they return lives in its parent, until the parent changes again.                                     */

// a deep copy into `a`, strings included. typed values are encoded into raw text on the way.
[[gnu::returns_nonnull]] cima_json *cima_json_clone(cilo_arena *a, const cima_json *v);
// sets `key`, replacing its value in place or appending it. returns the stored node.
cima_json *cima_json_set(cilo_arena *a, cima_json *object, const char *key, const cima_json *value);
// inserts `key` at `index`, clamped to the end. an existing `key` is removed first.
cima_json *cima_json_insert(cilo_arena *a, cima_json *object, size_t index, const char *key, const cima_json *value);
// appends `key` without looking for it and without copying it, for building big objects: the caller knows it is
// not there yet, or reads get the last one and both are written, and it outlives the object, a literal or a
// string in `a`. returns the stored node.
cima_json *cima_json_obj_push(cilo_arena *a, cima_json *object, const char *key, const cima_json *value);
bool cima_json_remove(cima_json *object, const char *key);
cima_json *cima_json_append(cilo_arena *a, cima_json *array, const cima_json *value);
bool cima_json_remove_at(cima_json *array, size_t index);

#define CIMA_JSON_SET(a, object, key, x) cima_json_set(a, object, key, CIMA_JSON_OF(x))
#define CIMA_JSON_INSERT(a, object, index, key, x) cima_json_insert(a, object, index, key, CIMA_JSON_OF(x))
#define CIMA_JSON_APPEND(a, array, x) cima_json_append(a, array, CIMA_JSON_OF(x))
#define CIMA_JSON_OBJ_PUSH(a, object, key, x) cima_json_obj_push(a, object, key, CIMA_JSON_OF(x))

/* comparing. */

// deep equality: object key order does not matter, numbers compare by value, so 1 and 1.0 are equal.
bool cima_json_eq(const cima_json *a, const cima_json *b);

typedef enum cima_json_change_kind : uint8_t {
  CIMA_JSON_ADDED = 1,
  CIMA_JSON_CHANGED,
  CIMA_JSON_REMOVED,
} cima_json_change_kind;

typedef struct cima_json_change {
  cima_json_change_kind kind;
  const char *key;
  size_t key_len;
  // null on the side the key is missing from.
  const cima_json *before;
  const cima_json *after;
} cima_json_change;

typedef struct cima_json_changes {
  cima_json_change *items;
  size_t len;
  // the keys both objects share come in another order.
  bool reordered;
} cima_json_changes;

// the members that differ between two objects, as livegraph diffs need them: added and changed keys in the
// order of `after`, then removed ones in the order of `before`. a missing object counts as an empty one.
cima_json_changes cima_json_diff(cilo_arena *a, const cima_json *before, const cima_json *after);
// `after` with the keys only `before` has set to null, so shallow merges forget them. anything that is not two
// objects gives `after`.
cima_json *cima_json_patch(cilo_arena *a, const cima_json *before, const cima_json *after);

/* writing. a writer appends json to a buffer from any allocator, an arena keeps growing it in place. commas, */
/* and the indentation of pretty output, are handled for you.                                                */
/* ========================================================================================================== */

struct cima_json_writer {
  char *buf;
  size_t len;
  size_t cap;
  // null means the heap.
  cilo_allocator *alloc;
  // spaces per level for pretty output, zero is compact.
  uint8_t indent;
  bool comma;
  bool after_key;
  uint32_t depth;
};

#define CIMA_JSON_WRITER(a, ...) ((cima_json_writer){.alloc = CILO_OF(a) __VA_OPT__(, ) __VA_ARGS__})

void cima_json_write_null(cima_json_writer *w);
void cima_json_write_bool(cima_json_writer *w, bool value);
void cima_json_write_int(cima_json_writer *w, int64_t value);
void cima_json_write_uint(cima_json_writer *w, uint64_t value);
// shortest text that reads back the same double, null for nan and infinities.
void cima_json_write_double(cima_json_writer *w, double value);
// a null string is written as null. invalid utf-8 becomes U+FFFD.
void cima_json_write_string(cima_json_writer *w, const char *s);
void cima_json_write_string_n(cima_json_writer *w, const char *s, size_t len);
void cima_json_write_raw(cima_json_writer *w, const char *json, size_t len);
void cima_json_write_key(cima_json_writer *w, const char *key);
void cima_json_write_key_n(cima_json_writer *w, const char *key, size_t len);
void cima_json_begin_object(cima_json_writer *w);
void cima_json_end_object(cima_json_writer *w);
void cima_json_begin_array(cima_json_writer *w);
void cima_json_end_array(cima_json_writer *w);
void cima_json_write(cima_json_writer *w, const cima_json *v);
// encodes a typed value in place, so a struct can sit inside an ad hoc envelope.
void cima_json_write_typed(cima_json_writer *w, const cima_schema *schema, const void *value);
// the text so far, nul terminated and still the writer's, valid until it writes again. "" for an empty writer.
[[gnu::returns_nonnull]] const char *cima_json_writer_text(cima_json_writer *w);
// nul terminates and hands the buffer over, `len` may be null. the writer is empty again.
[[gnu::returns_nonnull]] char *cima_json_writer_finish(cima_json_writer *w, size_t *len);
// drops the buffer of a writer on the heap or an allocator.
void cima_json_writer_dispose(cima_json_writer *w);

// numbers as the writer puts them, into `out` of CIMA_JSON_NUMBER_MAX bytes, nul terminated. they return the
// length. doubles get the shortest text that reads back the same, locale independent, `null` when not finite.
#define CIMA_JSON_NUMBER_MAX 32
size_t cima_json_format_double(char *out, double value);
size_t cima_json_format_int(char *out, int64_t value);
size_t cima_json_format_uint(char *out, uint64_t value);

// the value's type picks the format: `char buf[CIMA_JSON_NUMBER_MAX]; CIMA_JSON_FORMAT(buf, 0.1);`.
#define CIMA_JSON_FORMAT(out, x)                                                                                 \
  _Generic((x),                                                                                                  \
      float: cima_json_format_double,                                                                            \
      double: cima_json_format_double,                                                                           \
      unsigned char: cima_json_format_uint,                                                                      \
      unsigned short: cima_json_format_uint,                                                                     \
      unsigned int: cima_json_format_uint,                                                                       \
      unsigned long: cima_json_format_uint,                                                                      \
      unsigned long long: cima_json_format_uint,                                                                 \
      default: cima_json_format_int)(out, x)

// any builder value, picked like in CIMA_JSON_OBJ.
#define CIMA_JSON_PUT(w, x) cima_json_write(w, CIMA_JSON_OF(x))
#define CIMA_JSON_FIELD(w, key, x) (cima_json_write_key(w, key), CIMA_JSON_PUT(w, x))
#define CIMA_JSON_PUT_TYPED(w, node, ptr) cima_json_write_typed(w, CIMA_SCHEMA(node), CIMA_CHECKED(node, ptr))

// compact json text of `v` from `a`, nul terminated. `len` may be null.
[[gnu::returns_nonnull]] char *cima_json_text(cilo_allocator *a, const cima_json *v, size_t *len);
// the same, indented by two spaces.
[[gnu::returns_nonnull]] char *cima_json_pretty(cilo_allocator *a, const cima_json *v, size_t *len);

#ifdef __cplusplus
}
#endif

#endif
