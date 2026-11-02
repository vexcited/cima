#include "test.h"

enumeration(Role, admin, member, guest);

object(Address,
  string(at_least(1))            street,
  string(matches("^[0-9]{5}$"))  zip,
);

object(Profile,
  u32()                                           id,
  string(trim(), lowercase(), at_least(2))        handle,
  optional(u8(at_least(13)))                      age,
  nullable(string(url()))                         website,
  (Role)                                          role,
  array(string(), unique(), fallback(["c"]))      tags,
  optional(Address)                               address,
  number(at_least(0), at_most(1), fallback(0.5))  score,
);

static bool even(const void *value, cima_ctx *ctx) {
  if (*(const int32_t *)value % 2 == 0) return true;
  cima_report(ctx, "", "must be even");
  return false;
}

object(SignUp,
  string(email())          email,
  string(at_least(8))      password,
  string()                 confirm,
  i32(refine(even))        lucky,
  validate(passwords_match),
);

static bool passwords_match(const SignUp *value, cima_ctx *ctx) {
  if (strcmp(value->password, value->confirm) == 0) return true;
  cima_report(ctx, "/confirm", "passwords do not match");
  return false;
}

object(Circle, number(gt(0)) radius);
object(Rect, number(gt(0)) width, number(gt(0)) height, options(strict()));
tagged_union(Shape, "type", (Circle) circle, (Rect) rect);

object(Category,
  string()                              name,
  array(lazy(Category), fallback([]))   children,
);

object(Camel, u8() created_at, u8() user_id, options(rename_all(CAMEL), strict()));
object(Pascal, u8() created_at, options(rename_all(PASCAL)));
object(Kebab, u8() created_at, u8() _private_id, options(rename_all(KEBAB)));

object(Blob,
  bytes(at_most(4))                    data,
  timestamp()                          at,
  unknown()                            extra,
  array(i64(multiple_of(5)), nonempty())  steps,
);

object(Wide,
  i8() a, i16() b, i32() c, i64() d, u8() e, u16() f, u32() g, u64() h,
);

object(Odd, u8() value);

// a parse into an arena, the first issue as text.
#define PARSE(T, json, out) first_issue(a, CIMA_PARSE(T, json, out, .arena = a))

static void basics(void) {
  with_arena(a) {
    Profile p;
    CHECK_STR(PARSE(Profile, "{\"id\": 7, \"handle\": \"  AdA \", \"website\": null, \"role\": \"member\"}", &p), "ok");
    CHECK(p.id == 7 && strcmp(p.handle, "ada") == 0 && !p.age.some && !p.website.some && p.role == Role_member);
    CHECK(p.tags.len == 1 && strcmp(p.tags.items[0], "c") == 0 && p.tags.cap == 1);
    CHECK(p.score == 0.5 && !p.address.some);

    CHECK_STR(PARSE(Profile, "{\"id\": 1, \"handle\": \"ab\", \"website\": \"https://ciko.dev\", \"role\": \"guest\", "
                             "\"age\": 30, \"tags\": [], \"address\": {\"street\": \"x\", \"zip\": \"75001\"}}",
                    &p),
              "ok");
    CHECK(p.age.some && p.age.value == 30 && p.website.some && strcmp(p.website.value, "https://ciko.dev") == 0);
    CHECK(p.tags.len == 0 && p.address.some && strcmp(p.address.value.zip, "75001") == 0);

    // every issue at once, with json pointer paths.
    cima_result r = CIMA_PARSE(Profile,
                               "{\"handle\": \" A \", \"age\": 9, \"website\": \"nope\", \"role\": \"root\", \"tags\": "
                               "[\"x\", \"y\", \"x\"], \"address\": {\"street\": \"\", \"zip\": \"1\"}}",
                               &p, .arena = a);
    CHECK(!r.ok && r.issues.len == 8);
    const char *want[][2] = {
      {"/handle", "must be at least 2 characters long"},
      {"/age", "must be at least 13"},
      {"/website", "must be a valid url"},
      {"/role", "must be one of admin, member, guest"},
      {"/tags/2", "repeats item 0"},
      {"/address/street", "must be at least 1 characters long"},
      {"/address/zip", "must match ^[0-9]{5}$"},
      {"/id", "required"},
    };
    for (size_t i = 0; i < r.issues.len && i < 8; i++) {
      CHECK_STR(r.issues.items[i].path, want[i][0]);
      CHECK_STR(r.issues.items[i].message, want[i][1]);
    }
    CHECK(r.issues.items[0].code == CIMA_ISSUE_TOO_SMALL && r.issues.items[3].code == CIMA_ISSUE_INVALID_ENUM);
    CHECK(r.issues.items[4].code == CIMA_ISSUE_NOT_UNIQUE && r.issues.items[7].code == CIMA_ISSUE_REQUIRED);
    // a failed parse leaves nothing behind.
    CHECK(p.handle == nullptr && p.id == 0);

    CHECK_STR(PARSE(Profile, "{\"id\": \"1\"}", &p), "/id: expected an integer, got a string");
    CHECK_STR(PARSE(Profile, "[]", &p), ": expected an object, got an array");
    CHECK_STR(PARSE(Profile, "{\"id\": 1,}", &p), ": expected a key at offset 9");
    CHECK_STR(PARSE(Profile, "{\"id\": 1, \"id\": 2}", &p), "/id: duplicate key");
    CHECK_STR(PARSE(Profile, "", &p), ": empty input");
    CHECK_STR(PARSE(Odd, "{\"value\": 1} x", &(Odd){}), ": trailing characters at offset 13");
  }
}

static void numbers(void) {
  with_arena(a) {
    Wide w;
    CHECK_STR(PARSE(Wide,
                    "{\"a\": -128, \"b\": 32767, \"c\": -2147483648, \"d\": -9223372036854775808, \"e\": 255, \"f\": "
                    "65535, \"g\": 4294967295, \"h\": 18446744073709551615}",
                    &w),
              "ok");
    CHECK(w.a == INT8_MIN && w.b == INT16_MAX && w.c == INT32_MIN && w.d == INT64_MIN);
    CHECK(w.e == UINT8_MAX && w.f == UINT16_MAX && w.g == UINT32_MAX && w.h == UINT64_MAX);
    CHECK_STR(PARSE(i8(), "128", &(int8_t){}), ": must be at most 127");
    CHECK_STR(PARSE(i8(), "-129", &(int8_t){}), ": must be at least -128");
    CHECK_STR(PARSE(u8(), "-1", &(uint8_t){}), ": must be at least 0");
    CHECK_STR(PARSE(i64(), "9223372036854775808", &(int64_t){}), ": must be at most 9223372036854775807");
    CHECK_STR(PARSE(i64(), "-9223372036854775809", &(int64_t){}), ": must be at least -9223372036854775808");
    CHECK_STR(PARSE(u64(), "18446744073709551616", &(uint64_t){}), ": must be at most 18446744073709551615");
    CHECK_STR(PARSE(u64(), "1e30", &(uint64_t){}), ": must be at most 18446744073709551615");
    CHECK_STR(PARSE(i32(), "1.5", &(int32_t){}), ": expected an integer, got a fraction");
    // integral values written as reals are integers.
    int32_t i;
    CHECK_STR(PARSE(i32(), "1.50e1", &i), "ok");
    CHECK(i == 15);
    CHECK_STR(PARSE(i32(), "-0", &i), "ok");
    CHECK(i == 0);
    CHECK_STR(PARSE(i32(), "120000e-4", &i), "ok");
    CHECK(i == 12);
    CHECK_STR(PARSE(i32(gt(1), lt(5)), "5", &i), ": must be less than 5");
    CHECK_STR(PARSE(i32(gt(1), lt(5)), "1", &i), ": must be greater than 1");
    CHECK_STR(PARSE(i32(multiple_of(3)), "10", &i), ": must be a multiple of 3");
    CHECK_STR(PARSE(u64(at_most(10)), "18446744073709551615", &(uint64_t){}), ": must be at most 10");
    double d;
    CHECK_STR(PARSE(number(), "0.1", &d), "ok");
    CHECK(d == 0.1);
    CHECK_STR(PARSE(number(), "-1.7976931348623157e308", &d), "ok");
    CHECK(d == -1.7976931348623157e308);
    CHECK_STR(PARSE(number(), "4.9406564584124654e-324", &d), "ok");
    CHECK(d == 4.9406564584124654e-324);
    CHECK_STR(PARSE(number(), "1e400", &d), ": is out of the range of a double");
    CHECK_STR(PARSE(number(multiple_of(0.1)), "0.3", &d), "ok");
    CHECK_STR(PARSE(number(multiple_of(0.1)), "0.35", &d), ": must be a multiple of 0.1");
    CHECK_STR(PARSE(number(gt(0)), "0", &d), ": must be greater than 0");
    CHECK_STR(PARSE(number(), "true", &d), ": expected a number, got a boolean");
    // coercion reads numbers and booleans out of strings.
    cima_result r = CIMA_PARSE(Wide, "{\"a\": \"-1\", \"b\": \"2\", \"c\": \"3\", \"d\": \"4\", \"e\": \"5\", \"f\": \"6\", "
                                     "\"g\": \"7\", \"h\": \"8\"}",
                               &w, .arena = a, .coerce = true);
    CHECK(r.ok && w.a == -1 && w.h == 8);
    bool b;
    CHECK(CIMA_PARSE(boolean(), "\"true\"", &b, .arena = a, .coerce = true).ok && b);
    CHECK_STR(PARSE(boolean(), "\"true\"", &b), ": expected a boolean, got a string");
  }
}

static void strings(void) {
  with_arena(a) {
    cima_string s;
    // lengths count code points.
    CHECK_STR(PARSE(string(at_most(3)), "\"\xc3\xa9t\xc3\xa9\"", &s), "ok");
    CHECK_STR(PARSE(string(at_most(2)), "\"\xc3\xa9t\xc3\xa9\"", &s), ": must be at most 2 characters long");
    CHECK_STR(PARSE(string(), "\"a\\u00e9\\ud83d\\ude00\\n\\\"\"", &s), "ok");
    CHECK_STR(s, "a\xc3\xa9\xf0\x9f\x98\x80\n\"");
    CHECK_STR(PARSE(string(), "\"a\\u0000b\"", &s), ": must not hold \\u0000");
    CHECK_STR(PARSE(string(trim()), "\"  \\t x y \\n \"", &s), "ok");
    CHECK_STR(s, "x y");
    // trim and lowercase happen before the checks.
    CHECK_STR(PARSE(string(trim(), lowercase(), email()), "\" ADA@Example.COM \"", &s), "ok");
    CHECK_STR(s, "ada@example.com");
    CHECK_STR(PARSE(string(trim(), at_least(1)), "\"   \"", &s), ": must be at least 1 characters long");
    CHECK_STR(PARSE(string(starts_with("sk_"), ends_with("!")), "\"sk_x!\"", &s), "ok");
    CHECK_STR(PARSE(string(starts_with("sk_")), "\"pk_x\"", &s), ": must start with \"sk_\"");
    CHECK_STR(PARSE(string(ends_with("!")), "\"x\"", &s), ": must end with \"!\"");
    CHECK_STR(PARSE(string(matches("^[a-z]+(-[a-z]+)*$")), "\"kebab-case\"", &s), "ok");
    CHECK_STR(PARSE(string(matches("^[a-z]+$")), "\"Nope\"", &s), ": must match ^[a-z]+$");
    CHECK_STR(PARSE(string(matches("([")), "\"x\"", &s), ": the pattern ([ does not compile");
    CHECK_STR(PARSE(string(), "12", &s), ": expected a string, got a number");
  }
}

typedef struct format_case {
  cima_format format;
  const char *text;
  bool valid;
} format_case;

static void formats(void) {
  static const format_case cases[] = {
    {CIMA_FORMAT_EMAIL, "a@b.co", true},
    {CIMA_FORMAT_EMAIL, "first.last+tag@sub.example.com", true},
    {CIMA_FORMAT_EMAIL, "a@b", false},
    {CIMA_FORMAT_EMAIL, "@b.co", false},
    {CIMA_FORMAT_EMAIL, "a@.co", false},
    {CIMA_FORMAT_EMAIL, "a..b@c.co", false},
    {CIMA_FORMAT_EMAIL, "a@b.c", false},
    {CIMA_FORMAT_EMAIL, "plain", false},
    {CIMA_FORMAT_EMAIL, "a@b_c.com", false},
    {CIMA_FORMAT_URL, "https://ciko.dev", true},
    {CIMA_FORMAT_URL, "http://localhost:8080/x?y=1#z", true},
    {CIMA_FORMAT_URL, "https://user@[::1]:443/", true},
    {CIMA_FORMAT_URL, "mailto:a@b.co", true},
    {CIMA_FORMAT_URL, "ciko.dev", false},
    {CIMA_FORMAT_URL, "http://", false},
    {CIMA_FORMAT_URL, "https://exa mple.com", false},
    {CIMA_FORMAT_URL, "://x", false},
    {CIMA_FORMAT_URL, "http://host:port/", false},
    {CIMA_FORMAT_UUID, "123e4567-e89b-12d3-a456-426614174000", true},
    {CIMA_FORMAT_UUID, "123E4567-E89B-12D3-A456-426614174000", true},
    {CIMA_FORMAT_UUID, "123e4567e89b12d3a456426614174000", false},
    {CIMA_FORMAT_UUID, "123e4567-e89b-12d3-a456-42661417400g", false},
    {CIMA_FORMAT_DATE, "2024-02-29", true},
    {CIMA_FORMAT_DATE, "2023-02-29", false},
    {CIMA_FORMAT_DATE, "2024-13-01", false},
    {CIMA_FORMAT_DATE, "2024-1-01", false},
    {CIMA_FORMAT_DATETIME, "2026-10-29T13:47:10Z", true},
    {CIMA_FORMAT_DATETIME, "2026-10-29T13:47:10.123+02:00", true},
    {CIMA_FORMAT_DATETIME, "2026-10-29t13:47:10z", true},
    {CIMA_FORMAT_DATETIME, "2026-10-29T13:47:10", false},
    {CIMA_FORMAT_DATETIME, "2026-10-29T25:00:00Z", false},
    {CIMA_FORMAT_DATETIME, "2026-10-29", false},
    {CIMA_FORMAT_IPV4, "192.168.0.1", true},
    {CIMA_FORMAT_IPV4, "0.0.0.0", true},
    {CIMA_FORMAT_IPV4, "255.255.255.255", true},
    {CIMA_FORMAT_IPV4, "256.0.0.1", false},
    {CIMA_FORMAT_IPV4, "01.2.3.4", false},
    {CIMA_FORMAT_IPV4, "1.2.3", false},
    {CIMA_FORMAT_IPV4, "1.2.3.4.5", false},
    {CIMA_FORMAT_IPV6, "::1", true},
    {CIMA_FORMAT_IPV6, "::", true},
    {CIMA_FORMAT_IPV6, "2001:db8::8a2e:370:7334", true},
    {CIMA_FORMAT_IPV6, "1:2:3:4:5:6:7:8", true},
    {CIMA_FORMAT_IPV6, "::ffff:192.0.2.1", true},
    {CIMA_FORMAT_IPV6, "fe80::", true},
    {CIMA_FORMAT_IPV6, "1:2:3:4:5:6:7:8:9", false},
    {CIMA_FORMAT_IPV6, "1::2::3", false},
    {CIMA_FORMAT_IPV6, "12345::", false},
    {CIMA_FORMAT_IPV6, ":1", false},
    {CIMA_FORMAT_IPV6, "1:2:3:4:5:6:7", false},
    {CIMA_FORMAT_HOSTNAME, "example.com", true},
    {CIMA_FORMAT_HOSTNAME, "localhost", true},
    {CIMA_FORMAT_HOSTNAME, "a-b.c", true},
    {CIMA_FORMAT_HOSTNAME, "-a.com", false},
    {CIMA_FORMAT_HOSTNAME, "a-.com", false},
    {CIMA_FORMAT_HOSTNAME, "a..com", false},
    {CIMA_FORMAT_HOSTNAME, "a_b.com", false},
    {CIMA_FORMAT_HOSTNAME, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.com", false},
  };
  for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
    const format_case *t = &cases[i];
    cima_schema s = {.kind = CIMA_KIND_STRING, .size = sizeof(cima_string), .align = alignof(cima_string)};
    s.string.format = t->format;
    with_arena(a) {
      cima_string out;
      bool ok = cima_parse_text(&s, t->text, &out, &(cima_options){.arena = a}).ok;
      if (ok != t->valid) fprintf(stderr, "format %d: %s should be %s\n", t->format, t->text, t->valid ? "valid" : "invalid");
      CHECK(ok == t->valid);
    }
  }
  with_arena(a) {
    cima_string s;
    CHECK_STR(PARSE(string(ipv4()), "\"1.2.3\"", &s), ": must be a valid ipv4");
    CHECK_STR(PARSE(string(ipv6()), "\"::1\"", &s), "ok");
    CHECK_STR(PARSE(string(hostname()), "\"a_b\"", &s), ": must be a valid hostname");
    CHECK_STR(PARSE(string(uuid()), "\"x\"", &s), ": must be a valid uuid");
    CHECK_STR(PARSE(string(date()), "\"2024-02-30\"", &s), ": must be a valid date");
    CHECK_STR(PARSE(string(datetime()), "\"2024-02-03\"", &s), ": must be a valid datetime");
  }
}

static void composites(void) {
  with_arena(a) {
    Shape shape;
    CHECK_STR(PARSE(Shape, "{\"type\": \"circle\", \"radius\": 2}", &shape), "ok");
    CHECK(shape.kind == Shape_circle && shape.circle.radius == 2);
    // the tag may come last.
    CHECK_STR(PARSE(Shape, "{\"width\": 2, \"height\": 3, \"type\": \"rect\"}", &shape), "ok");
    CHECK(shape.kind == Shape_rect && shape.rect.width == 2 && shape.rect.height == 3);
    CHECK_STR(PARSE(Shape, "{\"type\": \"star\"}", &shape), "/type: must be one of circle, rect");
    CHECK_STR(PARSE(Shape, "{\"radius\": 1}", &shape), "/type: required");
    CHECK_STR(PARSE(Shape, "{\"type\": 1}", &shape), "/type: expected a string");
    CHECK_STR(PARSE(Shape, "{\"type\": \"rect\", \"width\": 1, \"height\": 1, \"depth\": 1}", &shape), "/depth: unknown key");
    CHECK_STR(PARSE(Shape, "{\"type\": \"circle\", \"radius\": -1}", &shape), "/radius: must be greater than 0");

    Category tree;
    CHECK_STR(PARSE(Category, "{\"name\": \"a\", \"children\": [{\"name\": \"b\", \"children\": [{\"name\": \"c\"}]}]}",
                    &tree),
              "ok");
    CHECK(tree.children.len == 1 && tree.children.items[0].children.len == 1);
    CHECK_STR(tree.children.items[0].children.items[0].name, "c");
    CHECK(tree.children.items[0].children.items[0].children.len == 0);
    CHECK_STR(PARSE(Category, "{\"name\": \"a\", \"children\": [{\"name\": \"b\", \"children\": [{}]}]}", &tree),
              "/children/0/children/0/name: required");

    Camel camel;
    CHECK_STR(PARSE(Camel, "{\"createdAt\": 1, \"userId\": 2}", &camel), "ok");
    CHECK(camel.created_at == 1 && camel.user_id == 2);
    CHECK_STR(PARSE(Camel, "{\"created_at\": 1, \"userId\": 2}", &camel), "/created_at: unknown key");
    Pascal pascal;
    CHECK_STR(PARSE(Pascal, "{\"CreatedAt\": 3, \"other\": true}", &pascal), "ok");
    CHECK(pascal.created_at == 3);
    Kebab kebab;
    CHECK_STR(PARSE(Kebab, "{\"created-at\": 4, \"_private-id\": 5}", &kebab), "ok");
    CHECK(kebab.created_at == 4 && kebab._private_id == 5);
    CHECK_STR(cima_to_json(&Kebab_schema, &kebab, &(cima_options){.arena = a}, nullptr),
              "{\"created-at\":4,\"_private-id\":5}");

    Blob blob;
    CHECK_STR(PARSE(Blob, "{\"data\": \"3q2+7w==\", \"at\": \"2000-01-01T02:00:00+02:00\", \"extra\": "
                          "{ \"any\" : [1, 2] }, \"steps\": [5, 10]}",
                    &blob),
              "ok");
    CHECK(blob.data.len == 4 && blob.data.ptr[0] == 0xde && blob.data.ptr[3] == 0xef);
    CHECK(blob.at == 946684800000);
    CHECK_STR(blob.extra, "{ \"any\" : [1, 2] }");
    CHECK(blob.steps.len == 2 && blob.steps.items[1] == 10);
    CHECK_STR(PARSE(Blob, "{\"data\": \"3q2+7w8=\", \"at\": \"2000-01-01\", \"extra\": null, \"steps\": [5]}", &blob),
              "/data: must be at most 4 bytes long");
    CHECK_STR(PARSE(Blob, "{\"data\": \"!!\", \"at\": \"2000-01-01\", \"extra\": 1, \"steps\": [5]}", &blob),
              "/data: must be valid base64");
    CHECK_STR(PARSE(Blob, "{\"data\": \"\", \"at\": \"yesterday\", \"extra\": 1, \"steps\": [5]}", &blob),
              "/at: must be an iso 8601 date and time");
    CHECK_STR(PARSE(Blob, "{\"data\": \"\", \"at\": \"2000-01-01\", \"extra\": 1, \"steps\": []}", &blob),
              "/steps: must hold at least 1 items");
    CHECK_STR(PARSE(Blob, "{\"data\": \"\", \"at\": \"2000-01-01\", \"extra\": 1, \"steps\": [7]}", &blob),
              "/steps/0: must be a multiple of 5");
    int64_t at;
    CHECK(CIMA_PARSE(timestamp(), "\"1969-12-31T23:59:59.999Z\"", &at, .arena = a).ok && at == -1);
    CHECK(CIMA_PARSE(timestamp(), "\"1970-01-01T00:00:00.1234Z\"", &at, .arena = a).ok && at == 123);

    // keys holding `/` and `~` are escaped in paths.
    typeof(CIMA_CTYPE(array(Odd))) odd;
    CHECK_STR(PARSE(array(Odd), "[{\"value\": 1}, {\"value\": 300}]", &odd), "/1/value: must be at most 255");
    cima_unknown any;
    CHECK(CIMA_PARSE(unknown(), " [1, {\"a/b~c\": 2}] ", &any, .arena = a).ok);
    CHECK_STR(any, "[1, {\"a/b~c\": 2}]");
  }
}

static void refinements(void) {
  with_arena(a) {
    SignUp s;
    CHECK_STR(PARSE(SignUp, "{\"email\": \"a@b.co\", \"password\": \"12345678\", \"confirm\": \"12345678\", \"lucky\": 4}", &s),
              "ok");
    CHECK_STR(PARSE(SignUp, "{\"email\": \"a@b.co\", \"password\": \"12345678\", \"confirm\": \"x\", \"lucky\": 4}", &s),
              "/confirm: passwords do not match");
    CHECK_STR(PARSE(SignUp, "{\"email\": \"a@b.co\", \"password\": \"12345678\", \"confirm\": \"12345678\", \"lucky\": 3}", &s),
              "/lucky: must be even");
    // object validators only run on values that passed their own checks.
    cima_result r = CIMA_PARSE(SignUp, "{\"email\": \"a\", \"password\": \"1\", \"confirm\": \"2\", \"lucky\": 3}", &s,
                               .arena = a);
    CHECK(r.issues.len == 3);
    CHECK(r.issues.items[2].code == CIMA_ISSUE_CUSTOM);
  }
}

static void limits(void) {
  with_arena(a) {
    Wide w;
    cima_result r = CIMA_PARSE(Wide, "{}", &w, .arena = a, .max_issues = 3);
    CHECK(!r.ok && r.issues.len == 3);
    r = CIMA_PARSE(Wide, "{}", &w, .arena = a);
    CHECK(!r.ok && r.issues.len == 8);
    cima_unknown any;
    r = CIMA_PARSE(unknown(), "[[[[1]]]]", &any, .arena = a, .max_depth = 3);
    CHECK_STR(first_issue(a, r), ": nested too deep at offset 3");
    CHECK(CIMA_PARSE(unknown(), "[[[[1]]]]", &any, .arena = a, .max_depth = 4).ok);
    Category tree;
    r = CIMA_PARSE(Category, "{\"name\": \"a\", \"children\": [{\"name\": \"b\", \"children\": [{\"name\": \"c\"}]}]}",
                   &tree, .arena = a, .max_depth = 2);
    CHECK(!r.ok && r.issues.items[0].code == CIMA_ISSUE_INVALID_JSON);
  }
}

static void checks(void) {
  with_arena(a) {
    Profile p = {.id = 1, .handle = "x", .role = (Role)7, .score = 2};
    p.tags = (cima_string_list){.items = (cima_string[]){"a", "a"}, .len = 2};
    cima_result r = cima_check(&Profile_schema, &p, &(cima_options){.arena = a});
    CHECK(!r.ok && r.issues.len == 4);
    CHECK_STR(r.issues.items[0].path, "/handle");
    CHECK_STR(r.issues.items[1].path, "/role");
    CHECK_STR(r.issues.items[2].path, "/tags/1");
    CHECK_STR(r.issues.items[3].path, "/score");
    p = (Profile){.id = 1, .handle = "xy", .score = 1, .website = {.some = true, .value = "https://a.co"}};
    CHECK(cima_check(&Profile_schema, &p, &(cima_options){.arena = a}).ok);
    p.handle = nullptr;
    CHECK_STR(first_issue(a, cima_check(&Profile_schema, &p, &(cima_options){.arena = a})), "/handle: required");
    Shape shape = {.kind = Shape_rect, .rect = {.width = 0, .height = 1}};
    CHECK_STR(first_issue(a, CIMA_CHECK(Shape, &shape, .arena = a)), "/width: must be greater than 0");
  }
}

int main(void) {
  basics();
  numbers();
  strings();
  formats();
  composites();
  refinements();
  limits();
  checks();
  return report_failures();
}
