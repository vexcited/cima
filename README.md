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
| `impl Deserialize, Serialize`, `z.codec`, `Type.Unsafe` | `custom(T, ctype, &codec)`, a [type of your own](#types-of-your-own) |
| `#[serde(with = "...")]` | `with((ctype), &codec) name`, one field through a codec |
| `serde_json::from_value` | `cima_decode()` from a codec, a DOM node through any schema |

A named type alone goes in parentheses, `(Role) role`, a node does not need them. `each(item, list)` walks
anything with `items` and `len`, a const list or one in a const struct too, and `value_or(maybe, x)` reads a `T_maybe`.

The short names come from `<cima/dsl.h>`. They are function like macros, so `string` or `array` used as plain
identifiers are left alone, and `<cima/dsl_end.h>` drops them. `<cima/cima.h>` alone only has the `CIMA_` names.

Code with names of its own like `matches` or `string` can take `<cima/json_dsl.h>` instead, the `json_` names of the DOM, the builder and the writer alone.

## Types of your own

A codec reads and writes a C type the way you want it, like an `impl Deserialize` and `impl Serialize` in serde. Its decoder gets the JSON value at hand as a DOM node, whatever the parser it comes from: a string for query strings and path segments, a node parsed on the spot for JSON. Its encoder writes one value with the writer, and its JSON Schema is a literal, so TypeBox style schemas stay right.

Discord's ids are a good case: a `u64` in C, a string of digits on the wire, though some payloads send a number.

```c
static bool snowflake_decode(const cima_json *in, void *out, cima_ctx *ctx) {
  uint64_t id = json_coerce(in, (uint64_t)0);                         // a number, or digits in a string
  if (!id) {
    cima_report(ctx, "", "must be a snowflake");                      // a path relative to the value
    return false;
  }
  *(uint64_t *)out = id;
  return true;
}

static void snowflake_encode(cima_json_writer *w, const void *value) {
  char digits[CIMA_JSON_NUMBER_MAX];
  cima_json_write_string_n(w, digits, cima_json_format_uint(digits, *(const uint64_t *)value));
}

static const cima_codec snowflake_codec = {
  .decode = snowflake_decode,
  .encode = snowflake_encode,
  .json_schema = literal({"type": "string", "pattern": "^[0-9]+$"}),
};

custom(Snowflake, uint64_t, &snowflake_codec, describe("a discord id"));

object(Member,
  (Snowflake)                          id,                            // a named type like any other
  array(Snowflake)                     roles,                         // Snowflake_list
  optional(Snowflake)                  nick_by,                       // Snowflake_maybe
  with((uint64_t), &snowflake_codec)   guild,                         // one field of a plain type
);
```

`custom()` declares `Snowflake`, `Snowflake_list`, `Snowflake_maybe` and `Snowflake_schema`, which the JSON Schema puts in `$defs` under its name with the codec's literal and the options. A decoder that keeps memory takes it with `cima_alloc()` or `cima_strdup()`, from the arena or allocator of the parse, and gives a `.dispose` for heap parses. One that wraps another schema decodes the node through it with `cima_decode()`, issues landing under the value: a point read from `[x, y]` or `{"x": .., "y": ..}` is a few lines. A decoder that returns false without a report gets a generic `is not valid` issue.

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

Microseconds per run and MB/s of JSON, the best of nine rounds of a release build, on an Apple M4 Pro with macOS 27 and Apple clang 21. `cima_bench` with no arguments measures the payloads below, and with JSON files as arguments it measures those, next to yyjson when CMake finds it installed. The three usual files come from the simdjson and nativejson-benchmark repositories: `twitter.json` is pretty printed with Japanese text and escaped URLs, `citm_catalog.json` is mostly indentation, small integers and short keys, and `canada.json` is coordinates. bun is 1.4.5 and its `JSON.parse` and `JSON.stringify`, nlohmann is the C++ `json` 3.12 from Homebrew, both measured the same way outside the bench.

| parse | cima `json_parse` | yyjson 0.13 | bun `JSON.parse` | nlohmann |
| --- | --- | --- | --- | --- |
| twitter.json, 632 KB | **127 us**, 4994 MB/s | 133 us, 4756 MB/s | 326 us, 1940 MB/s | 2897 us, 218 MB/s |
| citm_catalog.json, 1.7 MB | **317 us**, 5447 MB/s | 350 us, 4943 MB/s | 771 us, 2241 MB/s | 6896 us, 250 MB/s |
| canada.json, 2.3 MB | **1017 us**, 2214 MB/s | 1133 us, 1987 MB/s | 1747 us, 1289 MB/s | 15375 us, 146 MB/s |

| write | cima `json_text` | yyjson 0.13 | bun `JSON.stringify` | nlohmann |
| --- | --- | --- | --- | --- |
| twitter.json | **51 us**, 9194 MB/s | 73 us, 6415 MB/s | 134 us, 3488 MB/s | 1031 us, 453 MB/s |
| citm_catalog.json | **102 us**, 4921 MB/s | 106 us, 4738 MB/s | 1024 us, 488 MB/s | 971 us, 515 MB/s |
| canada.json | **1074 us**, 1946 MB/s | 1182 us, 1768 MB/s | 1290 us, 1621 MB/s | 5958 us, 351 MB/s |

Both the DOM parser and the writer are the fastest of the four on all three files.

The typed parser fills C structs straight from the text, with the checks of the schema, and takes about one and a half times as long as the DOM: every string is copied out into the value, every key is matched against the fields, and issues keep their JSON pointer.

| operation | 1 file, 1.6 KB | 100 files, 156 KB | font index, 2.4 MB pretty printed |
| --- | --- | --- | --- |
| typed parse, `cima_parse_json` | 0.6 us | 62 us, 2523 MB/s | 941 us, 2566 MB/s |
| typed encode, `cima_to_json` | 0.4 us | 39 us, 3974 MB/s | |
| DOM parse, `json_parse` | 0.4 us | 36 us, 4363 MB/s | 670 us, 3605 MB/s |
| DOM write, `json_text` | 0.2 us | 19 us, 8126 MB/s | |
| builder envelope around a typed value | 0.4 us | 38 us, 4151 MB/s | |
| validate only, `cima_json_valid` | 0.4 us | 34 us, 4573 MB/s | 735 us, 3284 MB/s |
| `yyjson_read`, for comparison | | | 762 us, 3169 MB/s |

The vektra payloads are the file object of `shape.rs`, 40 fields, in the `{error, status, meta, i18n}` envelope, alone and in a listing of 100. The font index is shaped like the one Figma serves and vektra2 reads: 3000 files of pretty printed objects full of short strings and small arrays.

`json_set` looks for its key before it appends a copy of it, so an object built with it costs a scan of its members per member. `json_obj_push` appends the member as is: the key is borrowed, a literal or a string in the arena, and must not be in the object yet, which a map from ids to nodes knows already.

| an object built member by member | 100 members | 10 000 members, 130 KB |
| --- | --- | --- |
| `json_set`, a lookup and a key copy | 16.1 us | 95 747 us, 1 MB/s |
| `json_obj_push`, appended as is | 0.8 us | 101 us, 1296 MB/s |

What makes it fast:

- **Both parsers read a copy of the text followed by 64 nul bytes.** Scans read whole words past the end without a bounds check, since a nul is never valid where one stops. The DOM keeps its strings in that copy, ended by a nul written over their closing quote, and decodes escaped ones in place.
- **Pretty printed text is guessed.** A pretty printer indents every child of a container alike, so the whitespace before the next one is taken to be what came before the last: the pointer moves on by a number known in advance, and only the check that the guess holds waits for the loads, with a branch that predicts. The closing bracket sits at the parent's guess.
- **Short strings and integers go a byte at a time with a branch each,** sixteen bytes for strings, nineteen digits for integers. The branches predict their lengths, so the loads of the next token start before the current one is done, where a word scan with a count of trailing zeros would make them wait. Longer strings go on eight bytes at a time: on an M4, NEON blocks lose to words there, since most runs end within a few words and a vector mask takes longer to reach a branch.
- **Text past ASCII comes in runs of one script.** The parsers check UTF-8 two three byte characters per word load while they last, Chinese and Japanese text for instance, then one character at a time. The writer checks the strings it did not parse 16 bytes at a time with NEON, after Keiser and Lemire, which makes Japanese text four times faster to write than a character at a time.
- **Numbers are read with Eisel-Lemire and written with Dragonbox,** over one table of 128 bit powers of ten. Dragonbox finds the shortest decimal from one product where Schubfach takes three, and Schubfach keeps the rare cases: powers of two, subnormals, and remainders on the edge of the interval. NEON turns the seventeen digits into text in one pass, finds the zeros at the end with one compare, and moves the fraction past the point with one table lookup. A fraction after an integer already read joins the mantissa without a second scan.
- **The writer keeps the end of its buffer in a local,** so writing text never makes the compiler reload it. It walks containers with a stack instead of recursing, puts a comma after every child and turns the last one into the closing bracket, checks the room once per child, writes empty containers without the stack, and copies keys and strings from a parse with fixed moves of 16 or 32 bytes. Strings the parser checked skip the UTF-8 check, and integers are formatted eight digits at a time.
- **Plain fields of the typed parser are read in place,** strings with no check, integers with no bounds and booleans, without the general path. The key a field expects next is compared with the text, quotes and colon included, before any scan or lookup. An object's fields and their JSON keys come from a cache built once per schema, and each parse keeps the last few at hand.
- Every allocation is an arena pointer bump, and a failed parse gives back everything it took.

Vector code sits behind a few helpers in `src/internal.h`, each with a scalar twin: NEON on arm64 for now, and other CPUs take the scalar paths. Defining `CIMA_NO_SIMD` forces them everywhere, which is how both are tested against each other.

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
release/cima_bench twitter.json citm_catalog.json canada.json      # any json files, yyjson next to them
```

The tests cover a JSON conformance suite, every option and format, round trips, JSON Schema output, pairs and
coercion, arenas against the heap and custom allocators, threads sharing the caches, and the DOM, writer,
builder and diff. They pass under ASan and UBSan.

Codecs are tested through decoding, encoding, schemas, text, pairs and heap disposal. Strings with every mix of escapes and UTF-8 are decoded at random against what they were made from, with bytes broken on purpose. Numbers are compared bit for bit with `strtod` on hundreds of millions of random decimals, halfway cases and subnormals, and the formatter with the reference it replaced, `CIMA_TEST_NUMBERS` and `CIMA_TEST_SEED` making the run longer or different. A build with `-DCMAKE_C_FLAGS=-DCIMA_NO_SIMD` runs the same tests on the scalar paths.
