#include "test.h"

enumeration(Role, admin, member);

object(Address, string(at_least(1)) street, options(strict()));

object(User,
  u32()                                                     id,
  string(trim(), at_least(1), at_most(64), describe("shown to others"))  display_name,
  string(email())                                           email,
  (Role)                                                    role,
  optional(Address)                                         address,
  nullable(string(url()))                                   website,
  array(string(), at_most(8), unique(), fallback([]))       tags,
  i64(gt(0), lt(100), multiple_of(5), example(25))          score,
  number(at_least(0.5))                                     ratio,
  string(starts_with("u."), ends_with("$"))                 handle,
  bytes()                                                   avatar,
  timestamp()                                               created_at,
  unknown()                                                 extra,
  options(rename_all(CAMEL)),
);

object(Circle, number(gt(0)) radius);
object(Rect, number() width, number() height);
tagged_union(Shape, "type", (Circle) circle, (Rect) rect);

object(Category, string() name, array(lazy(Category)) children);

object(Many,
  (Role) r00,
  (Role) r01,
  (Role) r02,
  (Role) r03,
  (Role) r04,
  (Role) r05,
  (Role) r06,
  (Role) r07,
  (Role) r08,
  (Role) r09,
  (Role) r10,
  (Role) r11,
  (Role) r12,
  (Role) r13,
  (Role) r14,
  (Role) r15,
  (Role) r16,
  (Role) r17,
  (Role) r18,
  (Role) r19,
  (Role) r20,
  (Role) r21,
  (Role) r22,
  (Role) r23,
  (Role) r24,
  (Role) r25,
  (Role) r26,
  (Role) r27,
  (Role) r28,
  (Role) r29,
  (Role) r30,
  (Role) r31,
  (Role) r32,
  (Role) r33,
  (Role) r34,
  (Role) r35,
  (Role) r36,
  (Role) r37,
  (Role) r38,
  (Role) r39,
  (Role) r40,
  (Role) r41,
  (Role) r42,
  (Role) r43,
  (Role) r44,
  (Role) r45,
  (Role) r46,
  (Role) r47,
  (Role) r48,
  (Role) r49,
  (Role) r50,
  (Role) r51,
  (Role) r52,
  (Role) r53,
  (Role) r54,
  (Role) r55,
  (Role) r56,
  (Role) r57,
  (Role) r58,
  (Role) r59,
);

static void documents(void) {
  with_arena(a) {
    CHECK_STR(CIMA_JSON_SCHEMA(u8(), .arena = a),
              "{\"$schema\":\"https://json-schema.org/draft/2020-12/schema\",\"type\":\"integer\",\"minimum\":0,"
              "\"maximum\":255}");
    CHECK_STR(CIMA_JSON_SCHEMA(array(nullable(i16(at_least(-5)))), .arena = a),
              "{\"$schema\":\"https://json-schema.org/draft/2020-12/schema\",\"type\":\"array\",\"items\":{\"anyOf\":["
              "{\"type\":\"integer\",\"minimum\":-5,\"maximum\":32767},{\"type\":\"null\"}]}}");
    CHECK_STR(CIMA_JSON_SCHEMA(unknown(describe("anything")), .arena = a),
              "{\"$schema\":\"https://json-schema.org/draft/2020-12/schema\",\"description\":\"anything\"}");
    CHECK_STR(CIMA_JSON_SCHEMA(User, .arena = a),
              "{\"$schema\":\"https://json-schema.org/draft/2020-12/schema\",\"$ref\":\"#/$defs/User\",\"$defs\":{"
              "\"User\":{\"type\":\"object\",\"properties\":{"
              "\"id\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":4294967295},"
              "\"displayName\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":64,"
              "\"description\":\"shown to others\"},"
              "\"email\":{\"type\":\"string\",\"format\":\"email\"},"
              "\"role\":{\"$ref\":\"#/$defs/Role\"},"
              "\"address\":{\"$ref\":\"#/$defs/Address\"},"
              "\"website\":{\"anyOf\":[{\"type\":\"string\",\"format\":\"uri\"},{\"type\":\"null\"}]},"
              "\"tags\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"maxItems\":8,\"uniqueItems\":true,"
              "\"default\":[]},"
              "\"score\":{\"type\":\"integer\",\"exclusiveMinimum\":0,\"exclusiveMaximum\":100,\"multipleOf\":5,"
              "\"examples\":[25]},"
              "\"ratio\":{\"type\":\"number\",\"minimum\":0.5},"
              "\"handle\":{\"type\":\"string\",\"allOf\":[{\"pattern\":\"^u\\\\.\"},{\"pattern\":\"\\\\$$\"}]},"
              "\"avatar\":{\"type\":\"string\",\"contentEncoding\":\"base64\"},"
              "\"createdAt\":{\"type\":\"string\",\"format\":\"date-time\"},"
              "\"extra\":{}},"
              "\"required\":[\"id\",\"displayName\",\"email\",\"role\",\"website\",\"score\",\"ratio\",\"handle\","
              "\"avatar\",\"createdAt\",\"extra\"]},"
              "\"Role\":{\"type\":\"string\",\"enum\":[\"admin\",\"member\"]},"
              "\"Address\":{\"type\":\"object\",\"properties\":{\"street\":{\"type\":\"string\",\"minLength\":1}},"
              "\"required\":[\"street\"],\"additionalProperties\":false}}}");
    CHECK_STR(CIMA_JSON_SCHEMA(Shape, .arena = a),
              "{\"$schema\":\"https://json-schema.org/draft/2020-12/schema\",\"$ref\":\"#/$defs/Shape\",\"$defs\":{"
              "\"Shape\":{\"oneOf\":["
              "{\"type\":\"object\",\"properties\":{\"type\":{\"const\":\"circle\"},\"radius\":{\"type\":\"number\","
              "\"exclusiveMinimum\":0}},\"required\":[\"type\",\"radius\"]},"
              "{\"type\":\"object\",\"properties\":{\"type\":{\"const\":\"rect\"},\"width\":{\"type\":\"number\"},"
              "\"height\":{\"type\":\"number\"}},\"required\":[\"type\",\"width\",\"height\"]}],"
              "\"discriminator\":{\"propertyName\":\"type\"}}}}");
    // a recursive type refers to itself.
    CHECK_STR(CIMA_JSON_SCHEMA(Category, .arena = a),
              "{\"$schema\":\"https://json-schema.org/draft/2020-12/schema\",\"$ref\":\"#/$defs/Category\",\"$defs\":{"
              "\"Category\":{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},\"children\":{\"type\":"
              "\"array\",\"items\":{\"$ref\":\"#/$defs/Category\"}}},\"required\":[\"name\",\"children\"]}}}");
    // every output is valid json.
    char *text = CIMA_JSON_SCHEMA(User, .arena = a);
    CHECK(cima_json_valid(text, strlen(text)));
  }
}

// how ciesta renders openapi: each operation's schema in place, the named types under components.
static void openapi(void) {
  with_arena(a) {
    cima_json_writer w = json_writer(a);
    cima_schema_defs defs = {.ref_prefix = "#/components/schemas/", .alloc = CILO_OF(a)};
    json_begin_object(&w);
    json_key(&w, "request");
    cima_json_schema_write(&w, &User_schema, &defs);
    json_key(&w, "response");
    cima_json_schema_write(&w, CIMA_SCHEMA(array(Shape)), &defs);
    json_key(&w, "components");
    json_begin_object(&w);
    json_key(&w, "schemas");
    cima_json_schema_write_defs(&w, &defs);
    json_end_object(&w);
    json_end_object(&w);
    char *text = json_finish(&w);
    cima_json *doc = json_parse(a, text, strlen(text), nullptr);
    CHECK(doc != nullptr);
    CHECK_STR(json_or(json_at(doc, "/request/$ref"), ""), "#/components/schemas/User");
    CHECK_STR(json_or(json_at(doc, "/response/items/$ref"), ""), "#/components/schemas/Shape");
    cima_json *schemas = json_at(doc, "/components/schemas");
    CHECK(json_len(schemas) == 4);
    const char *names[] = {"User", "Shape", "Role", "Address"};
    for (size_t i = 0; i < countof(names) && i < json_len(schemas); i++) CHECK_STR(schemas->members[i].key, names[i]);
    CHECK_STR(json_or(json_at(doc, "/components/schemas/User/properties/role/$ref"), ""),
              "#/components/schemas/Role");
  }
}

// many refs make the writer grow while a ref is written, the scratch space they use must stay apart.
static void growth(void) {
  for (int round = 0; round < 3; round++) {
    with_arena(a) {
      char *text = CIMA_JSON_SCHEMA(Many, .arena = a);
      cima_json *doc = json_parse(a, text, strlen(text), nullptr);
      CHECK(doc != nullptr);
      cima_json *props = json_at(doc, "/$defs/Many/properties");
      CHECK(json_len(props) == 60);
      each(m, json_members(props)) CHECK_STR(json_or(json_get(&m->value, "$ref"), ""), "#/$defs/Role");
    }
    char *heap = CIMA_JSON_SCHEMA(Many);
    CHECK(cima_json_valid(heap, strlen(heap)));
    cilo_mfree(heap);
  }
}

int main(void) {
  documents();
  growth();
  openapi();
  return report_failures();
}
