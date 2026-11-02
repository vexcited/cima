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
              "\"displayName\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":64,\"description\":\"shown to others\"},"
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
    for (size_t i = 0; i < 4 && i < json_len(schemas); i++) CHECK_STR(schemas->members[i].key, names[i]);
    CHECK_STR(json_or(json_at(doc, "/components/schemas/User/properties/role/$ref"), ""),
              "#/components/schemas/Role");
  }
}

int main(void) {
  documents();
  openapi();
  return report_failures();
}
