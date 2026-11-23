#include <cima/cima.h>
#include <stdcountof.h>
#include <stdio.h>
#include <string.h>

CIMA_OBJECT(Point,
  CIMA_NUMBER()                                x,
  CIMA_NUMBER()                                y,
  CIMA_OPTIONAL(CIMA_STRING(CIMA_AT_MOST(16)))  label,
);

static const char *kind_name(cima_kind kind) {
  switch (kind) {
  case CIMA_KIND_NUMBER:
    return "number";
  case CIMA_KIND_STRING:
    return "string";
  case CIMA_KIND_OPTIONAL:
    return "optional";
  default:
    return "other";
  }
}

int main(void) {
  // every schema is a constant struct, introspection needs nothing special.
  for (size_t i = 0; i < Point_schema.fields_count; i++) {
    const cima_field *f = &Point_schema.fields[i];
    printf("%s at +%zu: %s\n", f->name, f->offset, kind_name(f->schema->kind));
  }

  // a schema known only at runtime, say from a config file, parsing into raw memory.
  cima_field fields[] = {
    {.name = "host", .offset = 0, .schema = &cima_string_schema},
    {.name = "port", .offset = sizeof(cima_string), .schema = &cima_u16_schema},
  };
  cima_schema dynamic = {
    .kind = CIMA_KIND_OBJECT,
    .name = "Endpoint",
    .size = sizeof(cima_string) + sizeof(cima_u16) + 6,
    .align = alignof(cima_string),
    .fields = fields,
    .fields_count = countof(fields),
  };

  alignas(cima_string) unsigned char storage[sizeof(cima_string) + 8] = {};
  const char *json = "{\"host\": \"ciko.dev\", \"port\": 443}";
  cima_result result = cima_parse_json(&dynamic, json, strlen(json), storage, nullptr);
  bool ok = result.ok;
  if (ok) {
    cima_string host;
    cima_u16 port;
    memcpy(&host, storage, sizeof host);
    memcpy(&port, storage + sizeof(cima_string), sizeof port);
    printf("%s:%u\n", host, port);
  }
  // no arena: the heap holds the string, and the issues when there are some.
  cima_dispose(&dynamic, storage);
  cima_result_dispose(&result);
  return ok ? 0 : 1;
}
