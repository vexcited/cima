#include "internal.h"

#include <stdlib.h>

/* ========================================================================================================== */
/* typed values to json.                                                                                      */
/* ========================================================================================================== */

static void write_fields(cima_json_writer *w, const cima_schema *s, const char *v) {
  const cima__object_info *info = cima__object_info_of(s);
  for (size_t i = 0; i < s->fields_count; i++) {
    const cima_schema *fs = s->fields[i].schema;
    const char *field = v + s->fields[i].offset;
    if (fs->kind == CIMA_KIND_OPTIONAL && !*(const bool *)field) continue;
    if (LIKELY(!w->indent)) {
      cima__pre(w);
      cima__put(w, info->quoted[i], info->quoted_lens[i]);
      w->after_key = true;
    } else {
      cima_json_write_key_n(w, info->names[i], info->lens[i]);
    }
    cima_json_write_typed(w, fs, field);
  }
}

void cima_json_write_typed(cima_json_writer *w, const cima_schema *s, const void *value) {
  const char *v = value;
  s = cima__deref(s);
  switch (s->kind) {
  case CIMA_KIND_BOOL: cima_json_write_bool(w, *(const bool *)v); return;
  case CIMA_KIND_I8:
  case CIMA_KIND_I16:
  case CIMA_KIND_I32:
  case CIMA_KIND_I64:
  case CIMA_KIND_U8:
  case CIMA_KIND_U16:
  case CIMA_KIND_U32: cima_json_write_int(w, cima__read_int(s->kind, v)); return;
  case CIMA_KIND_U64: cima_json_write_uint(w, *(const uint64_t *)v); return;
  case CIMA_KIND_NUMBER: cima_json_write_double(w, *(const double *)v); return;
  case CIMA_KIND_STRING: cima_json_write_string(w, *(const cima_string *)v); return;
  case CIMA_KIND_BYTES: {
    const cima_bytes *b = (const cima_bytes *)v;
    cima__pre(w);
    char *d = cima__reserve(w, (b->len + 2) / 3 * 4 + 2);
    d[0] = '"';
    size_t n = cima__base64_encode(b->ptr, b->len, d + 1);
    d[n + 1] = '"';
    w->len += n + 2;
    w->comma = true;
    return;
  }
  case CIMA_KIND_TIMESTAMP: {
    cima__pre(w);
    char *d = cima__reserve(w, 34);
    d[0] = '"';
    size_t n = cima__fmt_timestamp(d + 1, *(const int64_t *)v);
    d[n + 1] = '"';
    w->len += n + 2;
    w->comma = true;
    return;
  }
  case CIMA_KIND_UNKNOWN: {
    const char *raw = *(const cima_unknown *)v;
    if (raw) cima_json_write_raw(w, raw, strlen(raw));
    else cima_json_write_null(w);
    return;
  }
  case CIMA_KIND_ARRAY: {
    const cima__list *list = (const cima__list *)v;
    cima_json_begin_array(w);
    for (size_t i = 0; i < list->len; i++) cima_json_write_typed(w, s->inner, list->items + i * s->inner->size);
    cima_json_end_array(w);
    return;
  }
  case CIMA_KIND_OPTIONAL:
  case CIMA_KIND_NULLABLE:
    if (*(const bool *)v) cima_json_write_typed(w, s->inner, v + s->value_offset);
    else cima_json_write_null(w);
    return;
  case CIMA_KIND_OBJECT:
    cima_json_begin_object(w);
    write_fields(w, s, v);
    cima_json_end_object(w);
    return;
  case CIMA_KIND_ENUM: {
    int64_t i = cima__read_enum(v, s->size);
    cima_json_write_string(w, i >= 0 && (size_t)i < s->names_count ? s->names[i] : nullptr);
    return;
  }
  case CIMA_KIND_TAGGED_UNION: {
    int64_t i = cima__read_enum(v + s->tag_offset, CIMA__KIND_SIZE);
    if (i < 0 || (size_t)i >= s->fields_count) {
      cima_json_write_null(w);
      return;
    }
    const cima_field *variant = &s->fields[i];
    const cima_schema *target = cima__deref(variant->schema);
    cima_json_begin_object(w);
    cima_json_write_key(w, s->tag);
    cima_json_write_string(w, variant->name);
    if (target->kind == CIMA_KIND_OBJECT) {
      write_fields(w, target, v + variant->offset);
    } else {
      cima_json_write_key(w, "value");
      cima_json_write_typed(w, target, v + variant->offset);
    }
    cima_json_end_object(w);
    return;
  }
  case CIMA_KIND_REF: return;
  }
}

static cilo_allocator *output_of(const cima_options *o) {
  if (!o) return cima__heap();
  if (o->arena) return CILO_OF(o->arena);
  return o->alloc ? o->alloc : cima__heap();
}

char *cima_to_json(const cima_schema *schema, const void *value, const cima_options *options, size_t *len) {
  cima_json_writer w = {.alloc = output_of(options)};
  cima_json_write_typed(&w, schema, value);
  return cima_json_writer_finish(&w, len);
}

/* ========================================================================================================== */
/* json schema, draft 2020-12.                                                                                */
/* ========================================================================================================== */

static bool is_named(const cima_schema *s) {
  return s->name && (s->kind == CIMA_KIND_OBJECT || s->kind == CIMA_KIND_ENUM || s->kind == CIMA_KIND_TAGGED_UNION);
}

static void defs_add(cima_schema_defs *defs, const cima_schema *s) {
  // the same type comes from every translation unit with its own address, names tell them apart.
  for (size_t i = 0; i < defs->len; i++)
    if (defs->items[i] == s || strcmp(defs->items[i]->name, s->name) == 0) return;
  if (defs->len == defs->cap) {
    cilo_allocator *a = defs->alloc ? defs->alloc : cima__heap();
    size_t cap = defs->cap ? defs->cap * 2 : 16;
    defs->items = cilo_realloc(a, defs->items, defs->cap * sizeof *defs->items, cap * sizeof *defs->items,
                               alignof(const cima_schema *));
    defs->cap = cap;
  }
  defs->items[defs->len++] = s;
}

void cima_schema_defs_dispose(cima_schema_defs *defs) {
  if (defs->items) cilo_free(defs->alloc ? defs->alloc : cima__heap(), defs->items, defs->cap * sizeof *defs->items);
  *defs = (cima_schema_defs){.ref_prefix = defs->ref_prefix, .alloc = defs->alloc};
}

// a scratch scope writing into `w` must not be the arena `w` grows in, its rewind would take the buffer along.
static cilo_arena *arena_of(const cima_json_writer *w) {
  return w->alloc && !*w->alloc ? (cilo_arena *)w->alloc : nullptr;
}

static void key_str(cima_json_writer *w, const char *key, const char *value) {
  cima_json_write_key(w, key);
  cima_json_write_string(w, value);
}

static void key_int(cima_json_writer *w, const char *key, int64_t value) {
  cima_json_write_key(w, key);
  cima_json_write_int(w, value);
}

static void key_uint(cima_json_writer *w, const char *key, uint64_t value) {
  cima_json_write_key(w, key);
  cima_json_write_uint(w, value);
}

static void key_double(cima_json_writer *w, const char *key, double value) {
  cima_json_write_key(w, key);
  cima_json_write_double(w, value);
}

// json literals written in schemas, normalized. text that does not parse is left out.
static void key_literal(cima_json_writer *w, const char *key, const char *json, bool as_list) {
  CILO_SCRATCH(tmp, arena_of(w)) {
    cima_json *v = cima_json_parse(tmp, json, strlen(json), nullptr);
    if (v) {
      cima_json_write_key(w, key);
      if (as_list) cima_json_begin_array(w);
      cima_json_write(w, v);
      if (as_list) cima_json_end_array(w);
    }
  }
}

static void write_ref(cima_json_writer *w, const cima_schema *s, cima_schema_defs *defs) {
  defs_add(defs, s);
  const char *prefix = defs->ref_prefix ? defs->ref_prefix : "#/$defs/";
  size_t p = strlen(prefix), n = strlen(s->name);
  cima_json_begin_object(w);
  cima_json_write_key(w, "$ref");
  CILO_SCRATCH(tmp, arena_of(w)) {
    char *ref = cilo_arena_alloc(tmp, p + n, 1);
    memcpy(ref, prefix, p);
    memcpy(ref + p, s->name, n);
    cima_json_write_string_n(w, ref, p + n);
  }
  cima_json_end_object(w);
}

static void annotations(cima_json_writer *w, const cima_schema *s) {
  if (s->common.doc) key_str(w, "description", s->common.doc);
  if (s->common.example) key_literal(w, "examples", s->common.example, true);
  if (s->common.default_json) key_literal(w, "default", s->common.default_json, false);
}

static void int_bounds(cima_json_writer *w, const cima_schema *s) {
  const cima_int_opts *o = &s->integer;
  int64_t lo = 0, hi = 0;
  bool natural = true;
  switch (s->kind) {
  case CIMA_KIND_I8: lo = INT8_MIN, hi = INT8_MAX; break;
  case CIMA_KIND_I16: lo = INT16_MIN, hi = INT16_MAX; break;
  case CIMA_KIND_I32: lo = INT32_MIN, hi = INT32_MAX; break;
  case CIMA_KIND_U8: lo = 0, hi = UINT8_MAX; break;
  case CIMA_KIND_U16: lo = 0, hi = UINT16_MAX; break;
  case CIMA_KIND_U32: lo = 0, hi = UINT32_MAX; break;
  default: natural = false; break;
  }
  if (o->has_min) key_int(w, o->exclusive_min ? "exclusiveMinimum" : "minimum", o->min);
  else if (natural || s->kind == CIMA_KIND_U64) key_int(w, "minimum", lo);
  if (o->has_max) key_int(w, o->exclusive_max ? "exclusiveMaximum" : "maximum", o->max);
  else if (natural) key_int(w, "maximum", hi);
  else if (s->kind == CIMA_KIND_U64) key_uint(w, "maximum", UINT64_MAX);
  if (o->multiple_of) key_int(w, "multipleOf", o->multiple_of);
}

// a posix extended pattern matching `text` as is at the start or the end.
static void literal_pattern(cima_json_writer *w, const char *text, bool at_end) {
  CILO_SCRATCH(tmp, arena_of(w)) {
    size_t n = strlen(text);
    char *p = cilo_arena_alloc(tmp, n * 2 + 2, 1), *d = p;
    if (!at_end) *d++ = '^';
    for (const char *c = text; *c; c++) {
      if (strchr(".[]()*+?{}|^$\\", *c)) *d++ = '\\';
      *d++ = *c;
    }
    if (at_end) *d++ = '$';
    cima_json_write_string_n(w, p, (size_t)(d - p));
  }
}

static void string_checks(cima_json_writer *w, const cima_schema *s) {
  static const char *const formats[] = {"", "email", "uri", "uuid", "date", "date-time", "ipv4", "ipv6", "hostname"};
  const cima_string_opts *o = &s->string;
  if (o->has_min) key_uint(w, "minLength", o->min);
  if (o->has_max) key_uint(w, "maxLength", o->max);
  if (o->format) key_str(w, "format", formats[o->format]);
  int patterns = !!o->pattern + !!o->starts_with + !!o->ends_with;
  if (patterns == 1) {
    cima_json_write_key(w, "pattern");
    if (o->pattern) cima_json_write_string(w, o->pattern);
    else literal_pattern(w, o->starts_with ? o->starts_with : o->ends_with, !o->starts_with);
  } else if (patterns > 1) {
    cima_json_write_key(w, "allOf");
    cima_json_begin_array(w);
    const char *texts[] = {o->pattern, o->starts_with, o->ends_with};
    for (int i = 0; i < 3; i++) {
      if (!texts[i]) continue;
      cima_json_begin_object(w);
      cima_json_write_key(w, "pattern");
      if (i == 0) cima_json_write_string(w, texts[i]);
      else literal_pattern(w, texts[i], i == 2);
      cima_json_end_object(w);
    }
    cima_json_end_array(w);
  }
}

static void write_schema(cima_json_writer *w, const cima_schema *s, cima_schema_defs *defs, bool define);

static bool required_field(const cima_schema *fs) {
  if (fs->kind == CIMA_KIND_OPTIONAL) return false;
  for (const cima_schema *t = fs;; t = t->inner) {
    if (t->common.default_json) return false;
    if (t->kind != CIMA_KIND_REF) return true;
  }
}

// properties and required of an object, the tag of a tagged union variant first when there is one.
static void object_body(cima_json_writer *w, const cima_schema *s, cima_schema_defs *defs, const char *tag,
                        const char *variant) {
  const cima__object_info *info = cima__object_info_of(s);
  key_str(w, "type", "object");
  cima_json_write_key(w, "properties");
  cima_json_begin_object(w);
  if (tag) {
    cima_json_write_key(w, tag);
    cima_json_begin_object(w);
    key_str(w, "const", variant);
    cima_json_end_object(w);
  }
  for (size_t i = 0; i < s->fields_count; i++) {
    const cima_schema *fs = s->fields[i].schema;
    cima_json_write_key_n(w, info->names[i], info->lens[i]);
    write_schema(w, fs->kind == CIMA_KIND_OPTIONAL ? fs->inner : fs, defs, false);
  }
  cima_json_end_object(w);
  bool any = tag != nullptr;
  for (size_t i = 0; i < s->fields_count && !any; i++) any = required_field(s->fields[i].schema);
  if (any) {
    cima_json_write_key(w, "required");
    cima_json_begin_array(w);
    if (tag) cima_json_write_string(w, tag);
    for (size_t i = 0; i < s->fields_count; i++)
      if (required_field(s->fields[i].schema)) cima_json_write_string_n(w, info->names[i], info->lens[i]);
    cima_json_end_array(w);
  }
  if (s->object.unknown_keys == CIMA_UNKNOWN_KEYS_REJECT) {
    cima_json_write_key(w, "additionalProperties");
    cima_json_write_bool(w, false);
  }
}

static void write_schema(cima_json_writer *w, const cima_schema *s, cima_schema_defs *defs, bool define) {
  if (s->kind == CIMA_KIND_REF) {
    write_schema(w, s->inner, defs, false);
    return;
  }
  if (is_named(s) && !define) {
    write_ref(w, s, defs);
    return;
  }
  if (s->kind == CIMA_KIND_OPTIONAL) {
    write_schema(w, s->inner, defs, false);
    return;
  }
  cima_json_begin_object(w);
  switch (s->kind) {
  case CIMA_KIND_BOOL: key_str(w, "type", "boolean"); break;
  case CIMA_KIND_I8:
  case CIMA_KIND_I16:
  case CIMA_KIND_I32:
  case CIMA_KIND_I64:
  case CIMA_KIND_U8:
  case CIMA_KIND_U16:
  case CIMA_KIND_U32:
  case CIMA_KIND_U64:
    key_str(w, "type", "integer");
    int_bounds(w, s);
    break;
  case CIMA_KIND_NUMBER: {
    const cima_number_opts *o = &s->number;
    key_str(w, "type", "number");
    if (o->has_min) key_double(w, o->exclusive_min ? "exclusiveMinimum" : "minimum", o->min);
    if (o->has_max) key_double(w, o->exclusive_max ? "exclusiveMaximum" : "maximum", o->max);
    if (o->multiple_of) key_double(w, "multipleOf", o->multiple_of);
    break;
  }
  case CIMA_KIND_STRING:
    key_str(w, "type", "string");
    string_checks(w, s);
    break;
  case CIMA_KIND_BYTES:
    key_str(w, "type", "string");
    key_str(w, "contentEncoding", "base64");
    break;
  case CIMA_KIND_TIMESTAMP:
    key_str(w, "type", "string");
    key_str(w, "format", "date-time");
    break;
  case CIMA_KIND_UNKNOWN: break;
  case CIMA_KIND_ARRAY: {
    const cima_array_opts *o = &s->array;
    key_str(w, "type", "array");
    cima_json_write_key(w, "items");
    write_schema(w, s->inner, defs, false);
    if (o->has_min) key_uint(w, "minItems", o->min);
    if (o->has_max) key_uint(w, "maxItems", o->max);
    if (o->unique) {
      cima_json_write_key(w, "uniqueItems");
      cima_json_write_bool(w, true);
    }
    break;
  }
  case CIMA_KIND_NULLABLE:
    cima_json_write_key(w, "anyOf");
    cima_json_begin_array(w);
    write_schema(w, s->inner, defs, false);
    cima_json_begin_object(w);
    key_str(w, "type", "null");
    cima_json_end_object(w);
    cima_json_end_array(w);
    break;
  case CIMA_KIND_OBJECT: object_body(w, s, defs, nullptr, nullptr); break;
  case CIMA_KIND_ENUM:
    key_str(w, "type", "string");
    cima_json_write_key(w, "enum");
    cima_json_begin_array(w);
    for (size_t i = 0; i < s->names_count; i++) cima_json_write_string(w, s->names[i]);
    cima_json_end_array(w);
    break;
  case CIMA_KIND_TAGGED_UNION:
    cima_json_write_key(w, "oneOf");
    cima_json_begin_array(w);
    for (size_t i = 0; i < s->fields_count; i++) {
      const cima_schema *target = cima__deref(s->fields[i].schema);
      cima_json_begin_object(w);
      if (target->kind == CIMA_KIND_OBJECT) {
        object_body(w, target, defs, s->tag, s->fields[i].name);
      } else {
        key_str(w, "type", "object");
        cima_json_write_key(w, "properties");
        cima_json_begin_object(w);
        cima_json_write_key(w, s->tag);
        cima_json_begin_object(w);
        key_str(w, "const", s->fields[i].name);
        cima_json_end_object(w);
        cima_json_write_key(w, "value");
        write_schema(w, target, defs, false);
        cima_json_end_object(w);
      }
      cima_json_end_object(w);
    }
    cima_json_end_array(w);
    cima_json_write_key(w, "discriminator");
    cima_json_begin_object(w);
    key_str(w, "propertyName", s->tag);
    cima_json_end_object(w);
    break;
  case CIMA_KIND_OPTIONAL:
  case CIMA_KIND_REF: break;
  }
  annotations(w, s);
  cima_json_end_object(w);
}

void cima_json_schema_write(cima_json_writer *w, const cima_schema *schema, cima_schema_defs *defs) {
  write_schema(w, schema, defs, false);
}

void cima_json_schema_write_defs(cima_json_writer *w, cima_schema_defs *defs) {
  cima_json_begin_object(w);
  // rendering a type may collect more, the loop picks them up.
  for (size_t i = 0; i < defs->len; i++) {
    cima_json_write_key(w, defs->items[i]->name);
    write_schema(w, defs->items[i], defs, true);
  }
  cima_json_end_object(w);
}

char *cima_json_schema(const cima_schema *schema, const cima_options *options) {
  cima_json_writer out = {.alloc = output_of(options)};
  CILO_SCRATCH(tmp, options ? options->arena : nullptr) {
    cima_schema_defs defs = {.alloc = CILO_OF(tmp)};
    cima_json_writer body = {.alloc = CILO_OF(tmp)};
    write_schema(&body, schema, &defs, false);
    cima_json_writer list = {.alloc = CILO_OF(tmp)};
    if (defs.len) cima_json_schema_write_defs(&list, &defs);
    static const char head[] = "{\"$schema\":\"https://json-schema.org/draft/2020-12/schema\"";
    cima__put(&out, head, sizeof head - 1);
    // the body is an object, its members go next to $schema.
    if (body.len > 2) {
      cima__putc(&out, ',');
      cima__put(&out, body.buf + 1, body.len - 2);
    }
    if (list.len) {
      cima__put(&out, ",\"$defs\":", 9);
      cima__put(&out, list.buf, list.len);
    }
    cima__putc(&out, '}');
  }
  return cima_json_writer_finish(&out, nullptr);
}
