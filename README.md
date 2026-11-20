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
| `Type.Strict(...)` JSON Schema | `cima_json_schema()`, named types in `$defs` |
| `z.coerce`, query strings | `cima_parse_pairs()`, `cima_parse_text()` with `.coerce = true` |
| `serde_json::Value`, `json!()` | `json_parse()`, `json_obj(...)`, the [DOM and builder](#json-without-a-schema) |

A named type alone goes in parentheses, `(Role) role`, a node does not need them. `each(item, list)` walks
anything with `items` and `len`, a const list or one in a const struct too, and `value_or(maybe, x)` reads a `T_maybe`.

The short names come from `<cima/dsl.h>`. They are function like macros, so `string` or `array` used as plain
identifiers are left alone, and `<cima/dsl_end.h>` drops them. `<cima/cima.h>` alone only has the `CIMA_` names.

## Memory

cima allocates through [cilo](../ciko-cilo). `if_parsed` parses into a thread scratch arena that is rewound when
the block ends. That frees the value, its strings and the issues with a single store, without walking the value
the way a malloc based `T_dispose` has to. In a loop, the same chunks get reused and nothing reaches the
system after the first iteration. Pass an arena as the fourth argument to keep the value.

Lists are shaped like cilo's `vec`, so `push(&arena, &user.tags, "new")` grows a parsed list in place.

Every call takes `.arena = &arena`, or `.alloc` for any other `cilo_allocator *`. With neither it is the
heap, which needs `T_dispose` and `cima_result_dispose`, and `cima_dispose_with` releases what `.alloc` holds.
A failed parse gives everything back but its issues, and leaves the value zeroed: an arena is rewound at once.
Parse options also turn on coercion for query strings and path segments, which is how ciesta uses it.

## JSON without a schema

Ad hoc JSON gets a DOM and a builder, `serde_json::Value` and `json!()` for C. The builder needs no allocator:
its nodes are compound literals that live until the end of the block, which is all a response needs, and
`_Generic` picks the kind of each value.

```c
char *body = json_text(a, json_obj(
    "error", false,
    "status", 200,
    "meta", json_obj("file", json_typed(File, &file), "tags", json_arr("a", 1, 2.5)),  // a typed value inside
    "i18n", nullptr,
));

cima_json *args = json_parse(a, text, len, &err);                  // any JSON, keys in order, all in `a`
int64_t size = json_or(json_get(args, "firstPageSize"), 25);       // the fallback's type picks the reader
const char *name = json_get_or(args, "name", nullptr);             // a member at once, the string or null
int64_t at = json_get_coerce(args, "created_at", (int64_t)0);      // z.coerce: "1795096047115" reads too
const char *name = json_or(json_at(args, "/files/0/name"), "?");   // JSON pointers, missing reads as the fallback
each(type, json_items(json_get(args, "resourceTypes"))) { ... }

cima_json *copy = json_clone(a, json_obj("id", id));               // a builder kept past its block
json_set(a, copy, "_denormUpsertIsTombstone_", false);             // set, insert, remove, append
json_obj_push(a, copy, key, node);                                  // no lookup, no key copy, for big objects
json_eq(old, now);                                                  // deep, key order and 1 vs 1.0 do not matter
each(change, json_diff(a, old, now)) { ... }                        // added, changed and removed keys, livegraph style
json_patch(a, old, now);                                            // `now` with the keys it lost set to null
```

When the shape follows a loop, a writer streams straight into an arena buffer, commas and indentation included:

```c
cima_json_writer w = json_writer(a, .indent = 2);
json_begin_object(&w);
json_key(&w, "files");
json_begin_array(&w);
each(file, files) json_put_typed(&w, File, file);                   // a typed value through its schema
json_end_array(&w);
json_field(&w, "total", files.len);
json_end_object(&w);
char *text = json_finish(&w);
```

`json_coerce` and `json_get_coerce` read numbers and booleans from strings too, for APIs that write big numbers as text, and a number as a boolean that is not zero. `cima_json_writer_text()` gives the text written so far, nul terminated and still the writer's, for a request body.

A comparison is an `int` in C, so a boolean from one is written `(bool)(a < b)`. Builder nodes are copied into
the arena by `json_clone` and by every function that stores them, typed values become their JSON on the way.

## What the runtime does

- **`cima_parse_json`** is a single pass that writes straight into the C value, guided by the schema, with no DOM
  in between. Strict RFC 8259 with UTF-8 validation and surrogate pairs, checked against an inline JSONTestSuite.
  Integers are read exactly, so `1.5e1` is a valid `i32` and `18446744073709551616` is one past `u64`. Doubles
  are correctly rounded and locale independent. Issues carry JSON pointer paths, `max_depth` and `max_issues`
  bound the work, and a tagged union finds its tag wherever it is in the object.
- **Checks** run on the C value, so `cima_check` shares them: lengths in code points after `trim` and
  `lowercase`, the formats `email`, `url`, `uuid`, `date`, `datetime`, `ipv4`, `ipv6` and `hostname`, POSIX
  extended patterns, unique arrays, then `refine` and `validate` functions that call `cima_report`.
- **Defaults** are parsed once per schema and copied into each value that misses them. Patterns compile once,
  and each object keeps its renamed keys and a key index. These caches are shared by every thread, read without
  a lock.
- **`cima_parse_pairs`** fills an object from a query string or a form, repeated keys or `key[]` fill arrays, and
  an empty input counts as left out when it is not text. **`cima_parse_text`** reads one value from a path
  segment or a header, comma separated for arrays.
- **`cima_to_json`** writes the shortest round tripping doubles, ISO 8601 timestamps with milliseconds, base64
  bytes, and leaves out optional fields that are not `some`. Invalid UTF-8 from C becomes U+FFFD.
- **`cima_json_schema`** renders draft 2020-12. To embed schemas in a larger document, `cima_json_schema_write`
  renders one in place with `$ref`s, and `cima_json_schema_write_defs` writes every named type it met, which is
  how ciesta's `OPENAPI()` fills `components.schemas`:

```c
cima_schema_defs defs = {.ref_prefix = "#/components/schemas/", .alloc = CILO_OF(a)};
cima_json_schema_write(&w, &NewPost_schema, &defs);                // {"$ref":"#/components/schemas/NewPost"}
cima_json_schema_write_defs(&w, &defs);                             // {"NewPost":{...},"Tag":{...}}
```

## Speed

`cima_bench`, a release build, the best of nine rounds. The payload is the file object of vektra's `shape.rs`,
40 fields, inside the `{error, status, meta, i18n}` envelope: one file of 1.6 KB, the size of most responses,
and a listing of 100 files, 156 KB. Microseconds per run, and MB/s of JSON.

| operation | macOS, 1 file | macOS, 100 files | Linux, 1 file | Linux, 100 files |
| --- | --- | --- | --- | --- |
| typed parse, `cima_parse_json` | 0.9 us | 86 us, 1815 MB/s | 0.9 us | 88 us, 1786 MB/s |
| typed encode, `cima_to_json` | 0.6 us | 59 us, 2644 MB/s | 0.5 us | 50 us, 3119 MB/s |
| DOM parse, `json_parse` | 0.5 us | 52 us, 3011 MB/s | 0.5 us | 55 us, 2847 MB/s |
| DOM write, `json_text` | 0.4 us | 42 us, 3739 MB/s | 0.3 us | 34 us, 4624 MB/s |
| builder envelope around a typed value | 0.6 us | 57 us, 2727 MB/s | 0.5 us | 48 us, 3229 MB/s |
| validate only, `cima_json_valid` | 0.4 us | 40 us, 3927 MB/s | 0.4 us | 42 us, 3757 MB/s |
| DOM parse, edit and diff one object | 1.9 us | 53 us, 2932 MB/s | 2.2 us | 56 us, 2796 MB/s |

The bench also generates a font index shaped like the one Figma serves and vektra2 reads: 3000 files, 2.4 MB of
pretty printed objects full of short strings and small arrays. "before" is the previous release. yyjson is the
Homebrew build on macOS and Debian's package on Linux, found by CMake only when installed and only for this row.

| font index, 2.4 MB | macOS before | macOS | Linux before | Linux |
| --- | --- | --- | --- | --- |
| DOM parse, `json_parse` | 1735 us, 1392 MB/s | 1037 us, 2329 MB/s | 1559 us, 1549 MB/s | 1045 us, 2312 MB/s |
| typed parse, `cima_parse_json` | 2005 us, 1205 MB/s | 1799 us, 1342 MB/s | 1831 us, 1319 MB/s | 1705 us, 1417 MB/s |
| validate only, `cima_json_valid` | 1040 us, 2323 MB/s | 968 us, 2494 MB/s | 1122 us, 2152 MB/s | 1020 us, 2367 MB/s |
| `yyjson_read`, for comparison | 777 us, 3107 MB/s | | 1289 us, 1873 MB/s | |

`json_set` looks for its key before it appends a copy of it, so an object built with it costs a scan of its
members per member. `json_obj_push` appends the member as is: the key is borrowed, a literal or a string in the
arena, and must not be in the object yet, which a map from ids to nodes knows already. Keys are made beforehand:

| an object built member by member | macOS, 100 members | macOS, 10 000 members, 130 KB |
| --- | --- | --- |
| `json_set`, a lookup and a key copy | 18.1 us | 105 211 us, 1 MB/s |
| `json_obj_push`, appended as is | 0.8 us | 105 us, 1241 MB/s |

macOS 27 with Apple clang 21, Linux 6 arm64 with Debian trixie and clang 19 in Docker's virtual machine, both on
an Apple M4 Pro.

- **The DOM parser copies the text once.** Strings and keys without escapes point into the copy, ended by a nul
  written over their closing quote, and escaped ones are decoded in place, so no string is allocated or copied on
  its own. A loop over a stack of open containers replaces the recursion, keeps its stacks in locals, and reads
  small integers without the general number scan. The arena holds the whole text, whitespace included, instead of
  the string bytes alone, and a failed parse gives back everything it took.
- **Strings are scanned eight bytes at a time** with word arithmetic, inlined where it pays: in the DOM and the
  validator, while the typed parser is faster calling out. NEON compares were slower on an M4 for strings this
  short, moving a vector mask to a register costs more than the scan.
- **Whitespace is a plain loop.** The indentation of pretty printed JSON repeats, the branch predictor learns it,
  and eight byte words were slower here. The DOM's copy ends with a nul, so its loop needs no bounds check.
- **Short keys and strings**, up to 16 bytes, are compared and copied with two overlapping words instead of
  calls to `memcmp` and `memcpy`.
- Integers and timestamps are formatted without `snprintf`, doubles take a correctly rounded fast path, and every
  allocation is an arena pointer bump.

## Formatting numbers

The writer's number formatting is public, for code that builds text itself:

```c
char buf[CIMA_JSON_NUMBER_MAX];
json_format(buf, 0.1);                                              // "0.1", the shortest text reading back the same
json_format(buf, -42);                                              // "-42", the value's type picks the format
size_t n = cima_json_format_double(buf, 1e300);                     // "1e+300", n is 6
```

`cima_json_format_double`, `cima_json_format_int` and `cima_json_format_uint` nul terminate and return the length.
Doubles are locale independent and give `null` when not finite, as the writer does.

## Building

It depends on cilo, fetched by CMake over https from `https://code.vexcited.com/ciko/cilo.git` unless a parent
project already has it or `FETCHCONTENT_SOURCE_DIR_CILO` points at a checkout. C23, clang 19 or gcc 14 and up,
macOS and Linux.

```sh
cmake -S . -B build -G Ninja -DFETCHCONTENT_SOURCE_DIR_CILO=../ciko-cilo
ninja -C build
ctest --test-dir build                                              # tests and examples
cmake -S . -B release -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_CILO=../ciko-cilo
ninja -C release cima_bench && release/cima_bench
```

The tests cover a JSON conformance suite, every option and format, round trips, JSON Schema output, pairs and
coercion, arenas against the heap and custom allocators, threads sharing the caches, and the DOM, writer,
builder and diff. They pass under ASan and UBSan.
