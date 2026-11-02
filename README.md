# cima

*Shape your data.* Zod style schemas for C, part of CiKo.

A schema is a constant `cima_schema` value. The macros that build it also emit the matching C type, so parsing
writes straight into a typed struct and the same definition validates, encodes and renders a JSON Schema. Fields
are written like a struct, type first:

```c
#include <cima/dsl.h>

enumeration(Role, admin, member, guest);

object(User,
  u32()                                       id,
  string(trim(), at_least(1), at_most(64))    name,
  string(email(), lowercase())                email,
  (Role)                                      role,
  array(string(), at_most(8), unique(), fallback([]))  tags,
  optional(u8(at_least(13)))                  age,
  validate(adult_admins),
);

static bool adult_admins(const User *user, cima_ctx *ctx) {     // a plain, typed function
  if (user->role != Role_admin || value_or(user->age, 0) >= 18)
    return true;
  cima_report(ctx, "/age", "admins must be adults");
  return false;
}

if_parsed(User, user, json) {                                     // z.safeParse
  each(tag, user.tags)
    puts(*tag);
} else {
  each(issue, issues)
    fprintf(stderr, "%s: %s\n", issue->path, issue->message);
}                                                                 // one store frees user and issues

if_parsed(User, kept, json, &arena) { ... }                       // parsed into `arena`, outlives the block
```

| Zod / TypeBox | cima |
| --- | --- |
| `z.object({...})`, `z.infer` | `object(T, node name, ...)` defines `T`, `T_schema`, `T_list`, `T_maybe`, `T_dispose` |
| `z.string().min(1).email()` | `string(at_least(1), email())`, options are compile checked per kind |
| `.optional()`, `.nullable()`, `.default(x)` | `optional(n)`, `nullable(n)`, `fallback(x)` with `x` written as JSON |
| `z.enum([...])` | `enumeration(T, a, b)`, a C enum with `T_name()` |
| `z.discriminatedUnion("type", [...])` | `tagged_union(T, "type", (A) a, (B) b)`, a tagged C union |
| `.extend()`, `.partial()` | a field list macro, `partial(FIELDS)` |
| `z.lazy` | `lazy(T)` |
| `.refine()` | `validate(fn)` on objects, `fn` a plain typed function, `refine(fn)` on fields |
| `.safeParse()` | `if_parsed(T, var, json) { ... } else { ... }`, or `CIMA_PARSE` |
| `Type.Strict(...)` JSON Schema | `cima_json_schema()` |

A named type alone goes in parentheses, `(Role) role`, a node does not need them. `each(item, list)` walks
anything with `items` and `len`, and `value_or(maybe, x)` reads a `T_maybe`.

The short names come from `<cima/dsl.h>`. They are function like macros, so `string` or `array` used as plain
identifiers are left alone, and `<cima/dsl_end.h>` drops them. `<cima/cima.h>` alone only has the `CIMA_` names.

## Memory

cima allocates through [cilo](../ciko-cilo). `if_parsed` parses into a thread scratch arena that is rewound when
the block ends. That frees the value, its strings and the issues with a single store, without walking the value
the way a malloc based `T_dispose` has to. In a loop, the same chunks get reused and nothing reaches the
system after the first iteration. Pass an arena as the fourth argument to keep the value.

Lists are shaped like cilo's `vec`, so `push(&arena, &user.tags, "new")` grows a parsed list in place.

Every call takes `.arena = &arena`, or `.alloc` for any other `cilo_allocator *`. With neither it is the
heap, which needs `T_dispose` and `cima_result_dispose`. Parse options also turn on coercion for query
strings and path segments, which is how ciesta uses it.

## Status

A draft: the headers and examples compile, the runtime (JSON parser, validator, encoder) is not written yet.

It depends on cilo, fetched by CMake unless `FETCHCONTENT_SOURCE_DIR_CILO` points at a checkout.

```sh
cmake -S . -B build -G Ninja -DFETCHCONTENT_SOURCE_DIR_CILO=../ciko-cilo
ninja -C build
```
