#ifndef CIMA_H
#define CIMA_H

#include <cilo/cilo.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================================================== */
/* the schema data model.                                                                                     */
/* ========================================================================================================== */

typedef enum cima_kind : uint8_t {
  CIMA_KIND_BOOL = 1,
  CIMA_KIND_I8,
  CIMA_KIND_I16,
  CIMA_KIND_I32,
  CIMA_KIND_I64,
  CIMA_KIND_U8,
  CIMA_KIND_U16,
  CIMA_KIND_U32,
  CIMA_KIND_U64,
  CIMA_KIND_NUMBER,
  CIMA_KIND_STRING,
  // base64 on the wire.
  CIMA_KIND_BYTES,
  // milliseconds since the epoch in c, an iso 8601 string on the wire.
  CIMA_KIND_TIMESTAMP,
  // any json value, kept as its raw text.
  CIMA_KIND_UNKNOWN,
  CIMA_KIND_ARRAY,
  // the key may be missing.
  CIMA_KIND_OPTIONAL,
  // the value may be null.
  CIMA_KIND_NULLABLE,
  CIMA_KIND_OBJECT,
  CIMA_KIND_ENUM,
  CIMA_KIND_TAGGED_UNION,
  // points at a named schema, rendered as a $ref.
  CIMA_KIND_REF,
} cima_kind;

typedef enum cima_format : uint8_t {
  CIMA_FORMAT_NONE,
  CIMA_FORMAT_EMAIL,
  CIMA_FORMAT_URL,
  CIMA_FORMAT_UUID,
  CIMA_FORMAT_DATE,
  CIMA_FORMAT_DATETIME,
  CIMA_FORMAT_IPV4,
  CIMA_FORMAT_IPV6,
  CIMA_FORMAT_HOSTNAME,
} cima_format;

// what an object does with keys it does not declare.
typedef enum cima_unknown_keys : uint8_t {
  CIMA_UNKNOWN_KEYS_STRIP,
  CIMA_UNKNOWN_KEYS_REJECT,
} cima_unknown_keys;

// how c field names map to json keys.
typedef enum cima_rename : uint8_t {
  CIMA_RENAME_NONE,
  CIMA_RENAME_CAMEL,
  CIMA_RENAME_PASCAL,
  CIMA_RENAME_KEBAB,
} cima_rename;

typedef struct cima_schema cima_schema;
typedef struct cima_ctx cima_ctx;

// releases what a parse on the heap put inside `value`, not `value` itself. arenas need none of it.
void cima_dispose(const cima_schema *schema, void *value);
// the same for a parse on `.alloc`.
void cima_dispose_with(const cima_schema *schema, void *value, cilo_allocator *alloc);

// custom check run after the built in ones, report problems with cima_report and return false.
typedef bool cima_refine_fn(const void *value, cima_ctx *ctx);

// options every kind accepts. example and default_json hold json text.
#define CIMA__COMMON_OPTS                                                                                        \
  const char *doc;                                                                                               \
  const char *example;                                                                                           \
  const char *default_json;                                                                                      \
  cima_refine_fn *refinement

typedef struct cima_common_opts {
  CIMA__COMMON_OPTS;
} cima_common_opts;

typedef struct cima_string_opts {
  CIMA__COMMON_OPTS;
  // in unicode code points.
  size_t min, max;
  bool has_min, has_max;
  cima_format format;
  // posix extended regex.
  const char *pattern;
  const char *starts_with, *ends_with;
  // applied before the checks.
  bool trim, lowercase;
} cima_string_opts;

typedef struct cima_int_opts {
  CIMA__COMMON_OPTS;
  int64_t min, max;
  bool has_min, has_max, exclusive_min, exclusive_max;
  int64_t multiple_of;
} cima_int_opts;

typedef struct cima_number_opts {
  CIMA__COMMON_OPTS;
  double min, max;
  bool has_min, has_max, exclusive_min, exclusive_max;
  double multiple_of;
} cima_number_opts;

typedef struct cima_array_opts {
  CIMA__COMMON_OPTS;
  size_t min, max;
  bool has_min, has_max, unique;
} cima_array_opts;

typedef struct cima_object_opts {
  CIMA__COMMON_OPTS;
  cima_unknown_keys unknown_keys;
  cima_rename rename;
} cima_object_opts;

// an object field, or a tagged union variant.
typedef struct cima_field {
  const char *name;
  size_t offset;
  const cima_schema *schema;
} cima_field;

struct cima_schema {
  cima_kind kind;
  // set on object, enum and tagged union schemas.
  const char *name;
  // of the c type.
  size_t size, align;
  // array items, optional and nullable values, ref targets.
  const cima_schema *inner;
  // optional and nullable, where `value` sits in the c struct.
  size_t value_offset;
  // object fields, tagged union variants.
  const cima_field *fields;
  size_t fields_count;
  // enum values.
  const char *const *names;
  size_t names_count;
  // tagged unions, the json key holding the variant name and where the c enum sits.
  const char *tag;
  size_t tag_offset;
  // every member starts with CIMA__COMMON_OPTS, so `common` reads them whatever the kind.
  union {
    cima_common_opts common;
    cima_string_opts string;
    cima_int_opts integer;
    cima_number_opts number;
    cima_array_opts array;
    cima_object_opts object;
  };
};

/* ========================================================================================================== */
/* c types of the primitives. every named type T comes with T_list and T_maybe.                               */
/* ========================================================================================================== */

typedef bool cima_bool;
typedef int8_t cima_i8;
typedef int16_t cima_i16;
typedef int32_t cima_i32;
typedef int64_t cima_i64;
typedef uint8_t cima_u8;
typedef uint16_t cima_u16;
typedef uint32_t cima_u32;
typedef uint64_t cima_u64;
typedef int64_t cima_integer;
typedef double cima_number;
// nul terminated, utf-8.
typedef const char *cima_string;
typedef struct cima_bytes {
  const uint8_t *ptr;
  size_t len;
} cima_bytes;
typedef int64_t cima_timestamp;
// raw json text, nul terminated.
typedef const char *cima_unknown;

// T_schema must be declared before. T_dispose fits [[gnu::cleanup]]. a list is shaped like a CILO_VEC, so
// CILO_PUSH grows it in place on an arena, and cap stays zero for lists that do not own their items.
#define CIMA__NAMED_EXTRAS(T)                                                                                    \
  typedef struct T##_list {                                                                                      \
    T *items;                                                                                                    \
    size_t len;                                                                                                  \
    size_t cap;                                                                                                  \
  } T##_list;                                                                                                    \
  typedef struct T##_maybe {                                                                                     \
    bool some;                                                                                                   \
    T value;                                                                                                     \
  } T##_maybe;                                                                                                   \
  [[maybe_unused]] static inline void T##_dispose(T *value) { cima_dispose(&T##_schema, value); }                \
  typedef T##_maybe T##_maybe

#define CIMA__PRIMITIVES(X)                                                                                      \
  X(bool) X(i8) X(i16) X(i32) X(i64) X(u8) X(u16) X(u32) X(u64) X(integer) X(number) X(string) X(bytes)          \
  X(timestamp) X(unknown)
#define CIMA__DECLARE_PRIMITIVE(t)                                                                               \
  extern const cima_schema cima_##t##_schema;                                                                    \
  CIMA__NAMED_EXTRAS(cima_##t);
CIMA__PRIMITIVES(CIMA__DECLARE_PRIMITIVE)

/* ========================================================================================================== */
/* schema nodes.                                                                                              */
/*                                                                                                            */
/* a node is a tuple `(tag, c type, (initializer))`, so nodes nest by plain macro argument expansion:         */
/*   cima__named  the c type is an identifier T, T_list and T_maybe exist.                                    */
/*   cima__typed  the c type is a parenthesized type name usable anywhere.                                    */
/*   cima__anon   the c type is a parenthesized anonymous struct, only declare it once.                       */
/* a bare identifier where a node is expected means CIMA_REF(identifier).                                     */
/* ========================================================================================================== */

#define CIMA__PRIM(t, K, member, ...)                                                                            \
  (cima__named, cima_##t,                                                                                        \
   (.kind = CIMA_KIND_##K, .size = sizeof(cima_##t), .align = alignof(cima_##t), .member = {__VA_ARGS__}))

#define CIMA_BOOLEAN(...) CIMA__PRIM(bool, BOOL, common, __VA_ARGS__)
#define CIMA_I8(...) CIMA__PRIM(i8, I8, integer, __VA_ARGS__)
#define CIMA_I16(...) CIMA__PRIM(i16, I16, integer, __VA_ARGS__)
#define CIMA_I32(...) CIMA__PRIM(i32, I32, integer, __VA_ARGS__)
#define CIMA_I64(...) CIMA__PRIM(i64, I64, integer, __VA_ARGS__)
#define CIMA_U8(...) CIMA__PRIM(u8, U8, integer, __VA_ARGS__)
#define CIMA_U16(...) CIMA__PRIM(u16, U16, integer, __VA_ARGS__)
#define CIMA_U32(...) CIMA__PRIM(u32, U32, integer, __VA_ARGS__)
#define CIMA_U64(...) CIMA__PRIM(u64, U64, integer, __VA_ARGS__)
#define CIMA_INTEGER(...) CIMA__PRIM(integer, I64, integer, __VA_ARGS__)
#define CIMA_NUMBER(...) CIMA__PRIM(number, NUMBER, number, __VA_ARGS__)
#define CIMA_STRING(...) CIMA__PRIM(string, STRING, string, __VA_ARGS__)
#define CIMA_BYTES(...) CIMA__PRIM(bytes, BYTES, string, __VA_ARGS__)
#define CIMA_TIMESTAMP(...) CIMA__PRIM(timestamp, TIMESTAMP, integer, __VA_ARGS__)
#define CIMA_UNKNOWN(...) CIMA__PRIM(unknown, UNKNOWN, common, __VA_ARGS__)

#define CIMA_REF(T)                                                                                              \
  (cima__named, T, (.kind = CIMA_KIND_REF, .size = sizeof(T), .align = alignof(T), .inner = &T##_schema))
// z.lazy: a reference usable inside T's own definition, arrays of it only.
#define CIMA_LAZY(T)                                                                                             \
  (cima__anon, (struct T),                                                                                       \
   (.kind = CIMA_KIND_REF, .size = sizeof(struct T), .align = alignof(struct T), .inner = &T##_schema))

// node plumbing: normalize, then open the tuple into `tag, type, (init)`.
#define CIMA__NODE(x) CIMA_PP_IF(CIMA_PP_IS_PAREN(x))(CIMA_PP_ID, CIMA_REF)(x)
#define CIMA__OPEN(x) CIMA__OPEN_I(CIMA__NODE(x))
#define CIMA__OPEN_I(t) CIMA_PP_UNPAREN t

// the c type of a node.
#define CIMA_CTYPE(node) CIMA__CTYPE_I(CIMA__OPEN(node))
#define CIMA__CTYPE_I(...) CIMA__CTYPE_II(__VA_ARGS__)
#define CIMA__CTYPE_II(tag, t, init) CIMA__CTYPE_##tag(t)
#define CIMA__CTYPE_cima__named(t) t
#define CIMA__CTYPE_cima__typed(t) CIMA_PP_UNPAREN t
#define CIMA__CTYPE_cima__anon(t) CIMA_PP_UNPAREN t

// the designated initializer of a node, and a pointer to it as a compound literal.
#define CIMA_INIT(node) CIMA__INIT_I(CIMA__OPEN(node))
#define CIMA__INIT_I(...) CIMA__INIT_II(__VA_ARGS__)
#define CIMA__INIT_II(tag, t, init) CIMA_PP_UNPAREN init
#define CIMA_SCHEMA(node) (&(const cima_schema){CIMA_INIT(node)})

// `ptr` type checked against the node when its c type can be named twice.
#define CIMA_CHECKED(node, ptr) CIMA__CHECKED_I(ptr, CIMA__OPEN(node))
#define CIMA__CHECKED_I(...) CIMA__CHECKED_II(__VA_ARGS__)
#define CIMA__CHECKED_II(ptr, tag, t, init) CIMA__CHECKED_##tag(ptr, t)
#define CIMA__CHECKED_cima__named(ptr, t) (1 ? (ptr) : (t *)nullptr)
#define CIMA__CHECKED_cima__typed(ptr, t) (1 ? (ptr) : (CIMA_PP_UNPAREN t *)nullptr)
#define CIMA__CHECKED_cima__anon(ptr, t) ((void *)(ptr))

// array(node, opts...) gives T_list for named nodes.
#define CIMA_ARRAY(node, ...) CIMA__ARRAY_I(CIMA__OPEN(node), __VA_ARGS__)
#define CIMA__ARRAY_I(...) CIMA__ARRAY_II(__VA_ARGS__)
#define CIMA__ARRAY_II(tag, t, init, ...) CIMA__ARRAY_##tag(t, init, __VA_ARGS__)
#define CIMA__ARRAY_cima__named(t, init, ...) CIMA__ARRAY_OF(cima__typed, (t##_list), init, __VA_ARGS__)
#define CIMA__ARRAY_cima__typed(t, init, ...)                                                                    \
  CIMA__ARRAY_OF(cima__anon, (struct { CIMA_PP_UNPAREN t *items; size_t len; }), init, __VA_ARGS__)
#define CIMA__ARRAY_cima__anon(t, init, ...) CIMA__ARRAY_cima__typed(t, init, __VA_ARGS__)
#define CIMA__ARRAY_OF(tag, ct, init, ...)                                                                       \
  (tag, ct,                                                                                                      \
   (.kind = CIMA_KIND_ARRAY, .size = sizeof(CIMA_PP_UNPAREN ct), .align = alignof(CIMA_PP_UNPAREN ct),           \
    .inner = &(const cima_schema){CIMA_PP_UNPAREN init}, .array = {__VA_ARGS__}))

// optional(node) and nullable(node) give T_maybe for named nodes. `bool some` is one byte, so `value` sits at the
// alignment of its type.
#define CIMA_OPTIONAL(node) CIMA__MAYBE_I(OPTIONAL, CIMA__OPEN(node))
#define CIMA_NULLABLE(node) CIMA__MAYBE_I(NULLABLE, CIMA__OPEN(node))
#define CIMA__MAYBE_I(...) CIMA__MAYBE_II(__VA_ARGS__)
#define CIMA__MAYBE_II(K, tag, t, init) CIMA__MAYBE_##tag(K, t, init)
#define CIMA__MAYBE_cima__named(K, t, init)                                                                      \
  CIMA__MAYBE_OF(K, cima__typed, (t##_maybe), offsetof(t##_maybe, value), init)
#define CIMA__MAYBE_cima__typed(K, t, init)                                                                      \
  CIMA__MAYBE_OF(K, cima__anon, (struct { bool some; CIMA_PP_UNPAREN t value; }), alignof(CIMA_PP_UNPAREN t),   \
                 init)
#define CIMA__MAYBE_cima__anon(K, t, init) CIMA__MAYBE_cima__typed(K, t, init)
#define CIMA__MAYBE_OF(K, tag, ct, offset, init)                                                                 \
  (tag, ct,                                                                                                      \
   (.kind = CIMA_KIND_##K, .size = sizeof(CIMA_PP_UNPAREN ct), .align = alignof(CIMA_PP_UNPAREN ct),             \
    .value_offset = offset, .inner = &(const cima_schema){CIMA_PP_UNPAREN init}))

/* ========================================================================================================== */
/* options, each expands to designators of the node's options struct, so a misplaced one fails to compile.   */
/* ========================================================================================================== */

#define CIMA_DESCRIBE(text) .doc = text
// json literals, written as is: CIMA_FALLBACK(20), CIMA_FALLBACK("guest"), CIMA_FALLBACK({"a": 1}).
#define CIMA_EXAMPLE(...) .example = #__VA_ARGS__
#define CIMA_FALLBACK(...) .default_json = #__VA_ARGS__
// a field level check, `bool fn(const void *value, cima_ctx *ctx)`. objects take typed ones with CIMA_VALIDATE.
#define CIMA_REFINE(fn) .refinement = fn

// length for strings and arrays, value for numbers.
#define CIMA_AT_LEAST(n) .min = (n), .has_min = true
#define CIMA_AT_MOST(n) .max = (n), .has_max = true
#define CIMA_GT(n) .min = (n), .has_min = true, .exclusive_min = true
#define CIMA_LT(n) .max = (n), .has_max = true, .exclusive_max = true
#define CIMA_MULTIPLE_OF(n) .multiple_of = (n)

#define CIMA_EMAIL() .format = CIMA_FORMAT_EMAIL
#define CIMA_URL() .format = CIMA_FORMAT_URL
#define CIMA_UUID() .format = CIMA_FORMAT_UUID
#define CIMA_DATE() .format = CIMA_FORMAT_DATE
#define CIMA_DATETIME() .format = CIMA_FORMAT_DATETIME
#define CIMA_IPV4() .format = CIMA_FORMAT_IPV4
#define CIMA_IPV6() .format = CIMA_FORMAT_IPV6
#define CIMA_HOSTNAME() .format = CIMA_FORMAT_HOSTNAME
#define CIMA_MATCHES(regex) .pattern = regex
#define CIMA_STARTS_WITH(s) .starts_with = s
#define CIMA_ENDS_WITH(s) .ends_with = s
#define CIMA_TRIM() .trim = true
#define CIMA_LOWERCASE() .lowercase = true

#define CIMA_UNIQUE() .unique = true
#define CIMA_NONEMPTY() .min = 1, .has_min = true

#define CIMA_STRICT() .unknown_keys = CIMA_UNKNOWN_KEYS_REJECT
#define CIMA_RENAME_ALL(style) .rename = CIMA_RENAME_##style

/* ========================================================================================================== */
/* named types: object, enumeration, tagged union. each also emits T_list, T_maybe and T_list_schema.         */
/* ========================================================================================================== */

// an item of an object or tagged union is written like a c declaration, the node then the name:
//
//   CIMA_STRING(CIMA_TRIM()) name,     CIMA_OPTIONAL(Address) address,     (Role) role,
//
// a named type alone goes in parentheses. the node expands to a tuple, so the item is `(tuple) name` and the
// leading parentheses split it. CIMA_FIELD(name, node) spells the same thing the other way around.
#define CIMA_FIELD(n, node) (cima__field, n, CIMA__NODE(node))
// object level options, anywhere in the list.
#define CIMA_OPTIONS(...) (cima__options, (__VA_ARGS__))
// zod's .refine() on an object: a plain `static bool fn(const T *value, cima_ctx *ctx)` defined anywhere after,
// its prototype comes with the object.
#define CIMA_VALIDATE(fn) (cima__validate, fn)
// every field of the list becomes optional, for patch bodies.
#define CIMA_PARTIAL(...) CIMA_PP_FOR_EACH(CIMA__PARTIAL_ITEM, __VA_ARGS__)

// item normalization: `(tuple) name` becomes `(cima__field, name, node)`, a tuple alone is kept.
#define CIMA__ITEM(...) __VA_OPT__(CIMA__NORM(__VA_ARGS__), )
#define CIMA__NORM(x) CIMA__NORM_I(CIMA__HEAD x)
#define CIMA__HEAD(...) (__VA_ARGS__),
#define CIMA__NORM_I(...) CIMA__NORM_II(__VA_ARGS__)
#define CIMA__NORM_II(head, ...)                                                                                 \
  CIMA_PP_IF(CIMA__HAS_ARGS(__VA_ARGS__))(CIMA__NORM_FIELD, CIMA__NORM_KEEP)(head, __VA_ARGS__)
#define CIMA__HAS_ARGS(...) CIMA_PP_FIRST(__VA_OPT__(1, ) 0)
#define CIMA__NORM_FIELD(head, n) (cima__field, n, CIMA__NODE_IN head)
#define CIMA__NORM_KEEP(head, ...) head
// `(Role)` is a reference, `(tag, type, init)` a node.
#define CIMA__NODE_IN(...) CIMA_PP_CAT(CIMA__NODE_IN_, CIMA__ARITY(__VA_ARGS__))(__VA_ARGS__)
#define CIMA__ARITY(...) CIMA__ARITY_I(__VA_ARGS__, 3, 2, 1, )
#define CIMA__ARITY_I(a, b, c, n, ...) n
#define CIMA__NODE_IN_1(x) CIMA__NODE(x)
#define CIMA__NODE_IN_3(...) (__VA_ARGS__)

#define CIMA__LIST_SCHEMA(T)                                                                                     \
  [[maybe_unused]] static const cima_schema T##_list_schema = {                                                 \
      .kind = CIMA_KIND_ARRAY, .size = sizeof(T##_list), .align = alignof(T##_list), .inner = &T##_schema}

// CIMA_OBJECT(User, CIMA_U32() id, CIMA_STRING() name, ...)
#define CIMA_OBJECT(T, ...) CIMA__OBJECT(T, CIMA_PP_FOR_EACH(CIMA__ITEM, __VA_ARGS__))
#define CIMA__OBJECT(T, ...)                                                                                     \
  typedef struct T T;                                                                                            \
  [[maybe_unused]] static const cima_schema T##_schema;                                                          \
  struct T {                                                                                                     \
    CIMA_PP_FOR_EACH(CIMA__OBJ_DECL, __VA_ARGS__)                                                                \
  };                                                                                                             \
  CIMA__NAMED_EXTRAS(T);                                                                                         \
  CIMA_PP_FOR_EACH_D(CIMA__OBJ_PROTO, T, __VA_ARGS__)                                                            \
  static const cima_field T##__fields[] = {CIMA_PP_FOR_EACH_D(CIMA__OBJ_FIELD, T, __VA_ARGS__)};                 \
  [[maybe_unused]] static const cima_schema T##_schema = {                                                       \
      .kind = CIMA_KIND_OBJECT,                                                                                  \
      .name = #T,                                                                                                \
      .size = sizeof(T),                                                                                         \
      .align = alignof(T),                                                                                       \
      .fields = T##__fields,                                                                                     \
      .fields_count = sizeof(T##__fields) / sizeof(cima_field),                                                  \
      .object = {CIMA_PP_FOR_EACH_D(CIMA__OBJ_OPTS, T, __VA_ARGS__)},                                                 \
  };                                                                                                             \
  CIMA__LIST_SCHEMA(T)

// CIMA_ENUMERATION(Role, admin, member) gives `enum Role { Role_admin, Role_member }`, "admin" and "member" in json,
// and Role_name(Role_admin) is "admin".
#define CIMA_ENUMERATION(T, ...)                                                                                 \
  typedef enum T { CIMA_PP_FOR_EACH_D(CIMA__ENUM_CONST, T, __VA_ARGS__) } T;                                     \
  [[maybe_unused]] static const cima_schema T##_schema;                                                          \
  CIMA__NAMED_EXTRAS(T);                                                                                         \
  static const char *const T##__names[] = {CIMA_PP_FOR_EACH(CIMA__ENUM_NAME, __VA_ARGS__)};                      \
  [[maybe_unused]] static inline const char *T##_name(T value) {                                                 \
    return (size_t)value < sizeof T##__names / sizeof *T##__names ? T##__names[value] : nullptr;                 \
  }                                                                                                              \
  [[maybe_unused]] static const cima_schema T##_schema = {                                                       \
      .kind = CIMA_KIND_ENUM,                                                                                    \
      .name = #T,                                                                                                \
      .size = sizeof(T),                                                                                         \
      .align = alignof(T),                                                                                       \
      .names = T##__names,                                                                                       \
      .names_count = sizeof(T##__names) / sizeof(*T##__names),                                                   \
  };                                                                                                             \
  CIMA__LIST_SCHEMA(T)

// z.discriminatedUnion. CIMA_TAGGED_UNION(Shape, "type", (Circle) circle, (Rect) rect) gives
// `struct Shape { Shape_kind kind; union { Circle circle; Rect rect; }; }` with Shape_circle and Shape_rect, and in
// json the payload's fields next to "type": "circle".
#define CIMA_VARIANT(n, node) (cima__variant, n, CIMA__NODE(node))
#define CIMA_TAGGED_UNION(T, tag_key, ...) CIMA__TAGGED_UNION(T, tag_key, CIMA_PP_FOR_EACH(CIMA__ITEM, __VA_ARGS__))
#define CIMA__TAGGED_UNION(T, tag_key, ...)                                                                      \
  typedef enum T##_kind { CIMA_PP_FOR_EACH_D(CIMA__TU_CONST, T, __VA_ARGS__) } T##_kind;                         \
  typedef struct T {                                                                                             \
    T##_kind kind;                                                                                               \
    union {                                                                                                      \
      CIMA_PP_FOR_EACH(CIMA__TU_MEMBER, __VA_ARGS__)                                                             \
    };                                                                                                           \
  } T;                                                                                                           \
  [[maybe_unused]] static const cima_schema T##_schema;                                                          \
  CIMA__NAMED_EXTRAS(T);                                                                                         \
  static const cima_field T##__variants[] = {CIMA_PP_FOR_EACH_D(CIMA__TU_VARIANT, T, __VA_ARGS__)};              \
  [[maybe_unused]] static const cima_schema T##_schema = {                                                       \
      .kind = CIMA_KIND_TAGGED_UNION,                                                                            \
      .name = #T,                                                                                                \
      .size = sizeof(T),                                                                                         \
      .align = alignof(T),                                                                                       \
      .fields = T##__variants,                                                                                   \
      .fields_count = sizeof(T##__variants) / sizeof(cima_field),                                                \
      .tag = tag_key,                                                                                            \
      .tag_offset = offsetof(T, kind),                                                                           \
  };                                                                                                             \
  CIMA__LIST_SCHEMA(T)

// list item plumbing, items are `(cima__field, name, node)`, `(cima__variant, name, node)`,
// `(cima__options, (designators))` or `(cima__validate, fn)`.
#define CIMA__OBJ_DECL(...) __VA_OPT__(CIMA__OBJ_DECL_I(CIMA_PP_UNPAREN __VA_ARGS__))
#define CIMA__OBJ_DECL_I(...) CIMA__OBJ_DECL_II(__VA_ARGS__)
#define CIMA__OBJ_DECL_II(tag, ...) CIMA__OBJ_DECL_##tag(__VA_ARGS__)
#define CIMA__OBJ_DECL_cima__field(n, node) CIMA_CTYPE(node) n;
#define CIMA__OBJ_DECL_cima__options(o)
#define CIMA__OBJ_DECL_cima__validate(fn)

// the validator's prototype, and an untyped trampoline for the schema.
#define CIMA__OBJ_PROTO(T, ...) __VA_OPT__(CIMA__OBJ_PROTO_I(T, CIMA_PP_UNPAREN __VA_ARGS__))
#define CIMA__OBJ_PROTO_I(...) CIMA__OBJ_PROTO_II(__VA_ARGS__)
#define CIMA__OBJ_PROTO_II(T, tag, ...) CIMA__OBJ_PROTO_##tag(T, __VA_ARGS__)
#define CIMA__OBJ_PROTO_cima__field(T, n, node)
#define CIMA__OBJ_PROTO_cima__options(T, o)
#define CIMA__OBJ_PROTO_cima__validate(T, fn)                                                                    \
  static bool fn(const T *value, cima_ctx *ctx);                                                                 \
  static bool T##__validate(const void *value, cima_ctx *ctx) { return fn(value, ctx); }

#define CIMA__OBJ_FIELD(T, ...) __VA_OPT__(CIMA__OBJ_FIELD_I(T, CIMA_PP_UNPAREN __VA_ARGS__))
#define CIMA__OBJ_FIELD_I(...) CIMA__OBJ_FIELD_II(__VA_ARGS__)
#define CIMA__OBJ_FIELD_II(T, tag, ...) CIMA__OBJ_FIELD_##tag(T, __VA_ARGS__)
#define CIMA__OBJ_FIELD_cima__field(T, n, node) {.name = #n, .offset = offsetof(T, n), .schema = CIMA_SCHEMA(node)},
#define CIMA__OBJ_FIELD_cima__options(T, o)
#define CIMA__OBJ_FIELD_cima__validate(T, fn)

#define CIMA__OBJ_OPTS(T, ...) __VA_OPT__(CIMA__OBJ_OPTS_I(T, CIMA_PP_UNPAREN __VA_ARGS__))
#define CIMA__OBJ_OPTS_I(...) CIMA__OBJ_OPTS_II(__VA_ARGS__)
#define CIMA__OBJ_OPTS_II(T, tag, ...) CIMA__OBJ_OPTS_##tag(T, __VA_ARGS__)
#define CIMA__OBJ_OPTS_cima__field(T, n, node)
#define CIMA__OBJ_OPTS_cima__options(T, o) CIMA_PP_UNPAREN o,
#define CIMA__OBJ_OPTS_cima__validate(T, fn) .refinement = T##__validate,

#define CIMA__PARTIAL_ITEM(...) __VA_OPT__(CIMA__PARTIAL_I(CIMA__OPEN_I(CIMA__NORM(__VA_ARGS__))))
#define CIMA__PARTIAL_I(...) CIMA__PARTIAL_II(__VA_ARGS__)
#define CIMA__PARTIAL_II(tag, ...) CIMA__PARTIAL_##tag(__VA_ARGS__)
#define CIMA__PARTIAL_cima__field(n, node) (cima__field, n, CIMA_OPTIONAL(node)),
#define CIMA__PARTIAL_cima__options(o) (cima__options, o),
#define CIMA__PARTIAL_cima__validate(fn)

#define CIMA__ENUM_CONST(T, ...) __VA_OPT__(CIMA_PP_CAT(T##_, __VA_ARGS__), )
#define CIMA__ENUM_NAME(...) __VA_OPT__(#__VA_ARGS__, )

#define CIMA__TU_CONST(T, ...) __VA_OPT__(CIMA__TU_CONST_I(T, CIMA_PP_UNPAREN __VA_ARGS__))
#define CIMA__TU_CONST_I(...) CIMA__TU_CONST_II(__VA_ARGS__)
#define CIMA__TU_CONST_II(T, tag, n, node) T##_##n,
#define CIMA__TU_MEMBER(...) __VA_OPT__(CIMA__TU_MEMBER_I(CIMA_PP_UNPAREN __VA_ARGS__))
#define CIMA__TU_MEMBER_I(...) CIMA__TU_MEMBER_II(__VA_ARGS__)
#define CIMA__TU_MEMBER_II(tag, n, node) CIMA_CTYPE(node) n;
#define CIMA__TU_VARIANT(T, ...) __VA_OPT__(CIMA__TU_VARIANT_I(T, CIMA_PP_UNPAREN __VA_ARGS__))
#define CIMA__TU_VARIANT_I(...) CIMA__TU_VARIANT_II(__VA_ARGS__)
#define CIMA__TU_VARIANT_II(T, tag, n, node) {.name = #n, .offset = offsetof(T, n), .schema = CIMA_SCHEMA(node)},

/* ========================================================================================================== */
/* runtime.                                                                                                   */
/* ========================================================================================================== */

typedef enum cima_issue_code : uint8_t {
  CIMA_ISSUE_INVALID_JSON = 1,
  CIMA_ISSUE_INVALID_TYPE,
  CIMA_ISSUE_REQUIRED,
  CIMA_ISSUE_TOO_SMALL,
  CIMA_ISSUE_TOO_BIG,
  CIMA_ISSUE_NOT_MULTIPLE,
  CIMA_ISSUE_INVALID_FORMAT,
  CIMA_ISSUE_PATTERN,
  CIMA_ISSUE_INVALID_ENUM,
  CIMA_ISSUE_INVALID_TAG,
  CIMA_ISSUE_NOT_UNIQUE,
  CIMA_ISSUE_UNKNOWN_KEY,
  CIMA_ISSUE_CUSTOM,
  CIMA_ISSUE_DUPLICATE_KEY,
  CIMA_ISSUE_TOO_DEEP,
} cima_issue_code;

// zod's ZodIssue. path is a json pointer such as "/tags/2".
typedef struct cima_issue {
  const char *path;
  cima_issue_code code;
  const char *message;
} cima_issue;

// shaped like every T_list, so CIMA_EACH walks it.
typedef struct cima_issues {
  const cima_issue *items;
  size_t len;
} cima_issues;

typedef struct cima_result {
  bool ok;
  cima_issues issues;
  // set when the issues live on the heap or `.alloc`, for cima_result_dispose.
  cilo_allocator *owner;
  size_t owned;
} cima_result;

typedef struct cima_options {
  // where parsed strings, arrays, issues and encodings go. an arena wins over alloc, and with neither it is the
  // heap, released with cima_dispose and cima_result_dispose.
  cilo_arena *arena;
  cilo_allocator *alloc;
  // read numbers and booleans from strings, for query strings, forms and path segments.
  bool coerce;
  // zero means 64.
  uint32_t max_depth;
  // zero means every issue.
  uint32_t max_issues;
} cima_options;

// a key and value pair of a query string or form, both decoded.
typedef struct cima_pair {
  const char *key;
  const char *value;
} cima_pair;

// parses and validates json into `out`, which must hold schema->size bytes. on failure `out` is zeroed and what
// the parse allocated is released, so only the issues need disposing.
cima_result cima_parse_json(const cima_schema *schema, const char *json, size_t len, void *out,
                            const cima_options *options);
// parses pairs into an object, repeated keys fill arrays.
cima_result cima_parse_pairs(const cima_schema *schema, const cima_pair *pairs, size_t count, void *out,
                             const cima_options *options);
// parses one scalar from text, such as a path segment.
cima_result cima_parse_text(const cima_schema *schema, const char *text, void *out, const cima_options *options);
// validates a value built in c.
cima_result cima_check(const cima_schema *schema, const void *value, const cima_options *options);
// encodes `value` as json, allocated from the options' allocator.
char *cima_to_json(const cima_schema *schema, const void *value, const cima_options *options, size_t *len);
// the json schema (draft 2020-12) of `schema`, typebox style: named types go to $defs and are used through $ref.
char *cima_json_schema(const cima_schema *schema, const cima_options *options);

// named types met while rendering schemas into a larger document, an openapi one for instance. zeroed is ready.
typedef struct cima_schema_defs {
  // put before the name in every $ref, "#/$defs/" when null. openapi wants "#/components/schemas/".
  const char *ref_prefix;
  // where the list grows, null for the heap.
  cilo_allocator *alloc;
  const cima_schema **items;
  size_t len;
  size_t cap;
} cima_schema_defs;

typedef struct cima_json_writer cima_json_writer;
// writes the schema of `schema` where `w` stands, named types as $ref, collected into `defs`.
void cima_json_schema_write(cima_json_writer *w, const cima_schema *schema, cima_schema_defs *defs);
// writes {"Name": {...}, ...} for every type collected so far, and the ones they reference in turn.
void cima_json_schema_write_defs(cima_json_writer *w, cima_schema_defs *defs);
void cima_schema_defs_dispose(cima_schema_defs *defs);

// adds an issue from a refinement, `path` is relative to the refined value. both strings are copied.
void cima_report(cima_ctx *ctx, const char *path, const char *message);
// the same with a code other than CIMA_ISSUE_CUSTOM.
void cima_report_code(cima_ctx *ctx, const char *path, cima_issue_code code, const char *message);

void cima_result_dispose(cima_result *result);

// typed front ends: the node gives the schema, and named nodes type check `out`.
#define CIMA_PARSE(node, json, out, ...)                                                                         \
  cima__parse_cstr(CIMA_SCHEMA(node), json, CIMA_CHECKED(node, out), &(const cima_options){__VA_ARGS__})
#define CIMA_CHECK(node, value, ...)                                                                             \
  cima_check(CIMA_SCHEMA(node), CIMA_CHECKED(node, value), &(const cima_options){__VA_ARGS__})
#define CIMA_TO_JSON(node, value, ...)                                                                           \
  cima_to_json(CIMA_SCHEMA(node), CIMA_CHECKED(node, value), &(const cima_options){__VA_ARGS__}, nullptr)
#define CIMA_JSON_SCHEMA(node, ...) cima_json_schema(CIMA_SCHEMA(node), &(const cima_options){__VA_ARGS__})
#define CIMA_DISPOSE(node, value) cima_dispose(CIMA_SCHEMA(node), CIMA_CHECKED(node, value))

/* ========================================================================================================== */
/* sugar.                                                                                                     */
/* ========================================================================================================== */

#define CIMA__UNIQUE(x) CIMA_PP_CAT(x, __LINE__)

typedef struct cima__parse_scope {
  cilo_mark mark;
  bool rewind;
} cima__parse_scope;

static inline void cima__parse_scope_end(cima__parse_scope *s) {
  if (s->rewind) cilo_rewind(s->mark);
}

#define CIMA__IN_SCRATCH() ((cima__parse_scope){cilo_mark_of(cilo__scratch(nullptr, 0)), true})
#define CIMA__IN_ARENA(a) ((cima__parse_scope){{.arena = (a)}, false})

// CIMA_IF_PARSED(User, user, json) { ... } else { ... }. the block sees `user`, the else branch the issues as
// `issues`. both live in a scratch arena, so the end of the block frees them with one store, with no walk over
// the value. CIMA_IF_PARSED(User, user, json, &arena) parses into `arena` instead, and the value outlives the
// block. T is a named type, a primitive is cima_u32 and so on.
#define CIMA_IF_PARSED(T, var, json, ...)                                                                        \
  for (cima__parse_scope CIMA__UNIQUE(cima__s) [[gnu::cleanup(cima__parse_scope_end)]] =                         \
           CIMA_PP_IF(CIMA__HAS_ARGS(__VA_ARGS__))(CIMA__IN_ARENA, CIMA__IN_SCRATCH)(__VA_ARGS__),               \
                       *CIMA__UNIQUE(cima__sp) = &CIMA__UNIQUE(cima__s);                                         \
       CIMA__UNIQUE(cima__sp); CIMA__UNIQUE(cima__sp) = nullptr)                                                 \
    for (T var = {}, *CIMA__UNIQUE(cima__vp) = &var; CIMA__UNIQUE(cima__vp); CIMA__UNIQUE(cima__vp) = nullptr)   \
      for (cima_result CIMA__UNIQUE(cima__r) =                                                                   \
               CIMA_PARSE(T, json, &var, .arena = CIMA__UNIQUE(cima__s).mark.arena),                    \
                       *CIMA__UNIQUE(cima__rp) = &CIMA__UNIQUE(cima__r);                                         \
           CIMA__UNIQUE(cima__rp); CIMA__UNIQUE(cima__rp) = nullptr)                                             \
        for (cima_issues issues = CIMA__UNIQUE(cima__r).issues, *CIMA__UNIQUE(cima__ip) = &issues;               \
             CIMA__UNIQUE(cima__ip); CIMA__UNIQUE(cima__ip) = nullptr)                                           \
          if (CIMA__UNIQUE(cima__r).ok)

// walks anything with `items` and `len`: every T_list, cima_issues, ciesta_headers. `var` points at the item.
#define CIMA_EACH(var, list)                                                                                     \
  for (typeof((list).items) var = (list).items; var < (list).items + (list).len; var++)

// the value of a T_maybe, or `fallback`.
#define CIMA_VALUE_OR(maybe, fallback) ((maybe).some ? (maybe).value : (fallback))

static inline cima_result cima__parse_cstr(const cima_schema *schema, const char *json, void *out,
                                           const cima_options *options) {
  return cima_parse_json(schema, json, json ? strlen(json) : 0, out, options);
}

#ifdef __cplusplus
}
#endif

#endif
