#include "internal.h"

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>

/* ========================================================================================================== */
/* state, paths and issues.                                                                                   */
/* ========================================================================================================== */

typedef struct frame {
  const char *key;
  size_t len;
  size_t index;
  bool is_index;
} frame;

typedef struct raw_issue {
  const char *path;
  const char *message;
  cima_issue_code code;
} raw_issue;

struct cima_ctx {
  const char *start, *p, *end;
  // where values go, and the scratch arena for everything that does not outlive the call.
  cilo_allocator *out;
  bool out_is_heap;
  cilo_arena *tmp;
  bool coerce;
  // a syntax error or the issue limit, everything unwinds.
  bool stop;
  uint32_t depth, max_depth, max_issues;
  frame *frames;
  size_t nframes, frames_cap;
  raw_issue *issues;
  size_t nissues, issues_cap;
};

static void push_frame(cima_ctx *c, frame f) {
  if (UNLIKELY(c->nframes == c->frames_cap)) {
    size_t cap = c->frames_cap ? c->frames_cap * 2 : 32;
    c->frames = cilo_arena_realloc(c->tmp, c->frames, c->frames_cap * sizeof(frame), cap * sizeof(frame),
                                   alignof(frame));
    c->frames_cap = cap;
  }
  c->frames[c->nframes++] = f;
}

static inline void push_key(cima_ctx *c, const char *key, size_t len) { push_frame(c, (frame){.key = key, .len = len}); }
static inline void push_index(cima_ctx *c, size_t i) { push_frame(c, (frame){.index = i, .is_index = true}); }
static inline void pop(cima_ctx *c) { c->nframes--; }

static void put_pointer_token(cima_json_writer *w, const char *key, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (key[i] == '~') cima__put(w, "~0", 2);
    else if (key[i] == '/') cima__put(w, "~1", 2);
    else cima__putc(w, key[i]);
  }
}

static const char *render_path(cima_ctx *c, const char *suffix) {
  cima_json_writer w = {.alloc = CILO_OF(c->tmp)};
  for (size_t i = 0; i < c->nframes; i++) {
    cima__putc(&w, '/');
    if (c->frames[i].is_index) {
      char digits[20];
      cima__put(&w, digits, cima__fmt_u64(digits, c->frames[i].index));
    } else {
      put_pointer_token(&w, c->frames[i].key, c->frames[i].len);
    }
  }
  if (suffix && *suffix) {
    if (*suffix != '/') cima__putc(&w, '/');
    cima__put(&w, suffix, strlen(suffix));
  }
  return cima_json_writer_finish(&w, nullptr);
}

static void report_at(cima_ctx *c, cima_issue_code code, const char *message, const char *suffix) {
  if (c->stop) return;
  if (UNLIKELY(c->nissues == c->issues_cap)) {
    size_t cap = c->issues_cap ? c->issues_cap * 2 : 8;
    c->issues = cilo_arena_realloc(c->tmp, c->issues, c->issues_cap * sizeof(raw_issue), cap * sizeof(raw_issue),
                                   alignof(raw_issue));
    c->issues_cap = cap;
  }
  c->issues[c->nissues++] = (raw_issue){render_path(c, suffix), message, code};
  if (c->max_issues && c->nissues >= c->max_issues) c->stop = true;
}

static void report(cima_ctx *c, cima_issue_code code, const char *message) { report_at(c, code, message, nullptr); }

[[gnu::format(printf, 3, 4)]] static void reportf(cima_ctx *c, cima_issue_code code, const char *fmt, ...) {
  if (c->stop) return;
  va_list ap;
  va_start(ap, fmt);
  char buf[256];
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  report(c, code, cilo_strdup(CILO_OF(c->tmp), buf));
}

static void syntax(cima_ctx *c, const char *at, const char *message) {
  if (c->stop) return;
  reportf(c, CIMA_ISSUE_INVALID_JSON, "%s at offset %zu", message, (size_t)(at - c->start));
  c->stop = true;
}

void cima_report_code(cima_ctx *c, const char *path, cima_issue_code code, const char *message) {
  report_at(c, code, cilo_strdup(CILO_OF(c->tmp), message ? message : ""), path);
}

void cima_report(cima_ctx *c, const char *path, const char *message) {
  cima_report_code(c, path, CIMA_ISSUE_CUSTOM, message);
}

static inline void refine(cima_ctx *c, const cima_schema *s, const void *value, size_t before) {
  if (s->common.refinement && !c->stop && c->nissues == before) s->common.refinement(value, c);
}

/* ========================================================================================================== */
/* checks on values in c, shared by every parser and by cima_check.                                           */
/* ========================================================================================================== */

static size_t code_points(const char *s, size_t len) {
  size_t n = 0;
  for (size_t i = 0; i < len; i++) n += ((unsigned char)s[i] & 0xc0) != 0x80;
  return n;
}

static const char *format_name(cima_format f) {
  static const char *const names[] = {"", "email", "url", "uuid", "date", "datetime", "ipv4", "ipv6", "hostname"};
  return names[f];
}

static void check_string(cima_ctx *c, const cima_schema *s, const char *str, size_t len) {
  const cima_string_opts *o = &s->string;
  if (o->has_min || o->has_max) {
    size_t n = code_points(str, len);
    if (o->has_min && n < o->min) reportf(c, CIMA_ISSUE_TOO_SMALL, "must be at least %zu characters long", o->min);
    if (o->has_max && n > o->max) reportf(c, CIMA_ISSUE_TOO_BIG, "must be at most %zu characters long", o->max);
  }
  if (o->format && !cima__check_format(o->format, str, len))
    reportf(c, CIMA_ISSUE_INVALID_FORMAT, "must be a valid %s", format_name(o->format));
  if (o->pattern) {
    const void *re = cima__regex_of(o->pattern);
    if (!re) reportf(c, CIMA_ISSUE_PATTERN, "the pattern %s does not compile", o->pattern);
    else if (!cima__regex_match(re, str)) reportf(c, CIMA_ISSUE_PATTERN, "must match %s", o->pattern);
  }
  if (o->starts_with && strncmp(str, o->starts_with, strlen(o->starts_with)) != 0)
    reportf(c, CIMA_ISSUE_PATTERN, "must start with \"%s\"", o->starts_with);
  if (o->ends_with) {
    size_t n = strlen(o->ends_with);
    if (n > len || memcmp(str + len - n, o->ends_with, n) != 0)
      reportf(c, CIMA_ISSUE_PATTERN, "must end with \"%s\"", o->ends_with);
  }
}

static void check_bytes(cima_ctx *c, const cima_schema *s, size_t len) {
  const cima_string_opts *o = &s->string;
  if (o->has_min && len < o->min) reportf(c, CIMA_ISSUE_TOO_SMALL, "must be at least %zu bytes long", o->min);
  if (o->has_max && len > o->max) reportf(c, CIMA_ISSUE_TOO_BIG, "must be at most %zu bytes long", o->max);
}

// `big` is a u64 past INT64_MAX, held in `u`.
static void check_int(cima_ctx *c, const cima_schema *s, bool big, int64_t i, uint64_t u) {
  const cima_int_opts *o = &s->integer;
  if (o->has_min && !big && (o->exclusive_min ? i <= o->min : i < o->min))
    reportf(c, CIMA_ISSUE_TOO_SMALL, o->exclusive_min ? "must be greater than %lld" : "must be at least %lld",
            (long long)o->min);
  if (o->has_max && (big || (o->exclusive_max ? i >= o->max : i > o->max)))
    reportf(c, CIMA_ISSUE_TOO_BIG, o->exclusive_max ? "must be less than %lld" : "must be at most %lld",
            (long long)o->max);
  if (o->multiple_of) {
    bool ok = big ? o->multiple_of > 0 && u % (uint64_t)o->multiple_of == 0 : i % o->multiple_of == 0;
    if (!ok) reportf(c, CIMA_ISSUE_NOT_MULTIPLE, "must be a multiple of %lld", (long long)o->multiple_of);
  }
}

static void check_number(cima_ctx *c, const cima_schema *s, double d) {
  const cima_number_opts *o = &s->number;
  if (o->has_min && (o->exclusive_min ? d <= o->min : d < o->min))
    reportf(c, CIMA_ISSUE_TOO_SMALL, o->exclusive_min ? "must be greater than %g" : "must be at least %g", o->min);
  if (o->has_max && (o->exclusive_max ? d >= o->max : d > o->max))
    reportf(c, CIMA_ISSUE_TOO_BIG, o->exclusive_max ? "must be less than %g" : "must be at most %g", o->max);
  if (o->multiple_of) {
    double q = d / o->multiple_of;
    if (fabs(q - nearbyint(q)) > 1e-9 * fmax(1, fabs(q)))
      reportf(c, CIMA_ISSUE_NOT_MULTIPLE, "must be a multiple of %g", o->multiple_of);
  }
}

static void check_list(cima_ctx *c, const cima_schema *s, const cima__list *list) {
  const cima_array_opts *o = &s->array;
  if (o->has_min && list->len < o->min) reportf(c, CIMA_ISSUE_TOO_SMALL, "must hold at least %zu items", o->min);
  if (o->has_max && list->len > o->max) reportf(c, CIMA_ISSUE_TOO_BIG, "must hold at most %zu items", o->max);
  if (!o->unique || list->len < 2) return;
  const cima_schema *item = s->inner;
  size_t size = item->size, n = list->len;
  if (n <= 16) {
    for (size_t j = 1; j < n; j++) {
      for (size_t i = 0; i < j; i++) {
        if (cima__value_eq(item, list->items + i * size, list->items + j * size)) {
          push_index(c, j);
          reportf(c, CIMA_ISSUE_NOT_UNIQUE, "repeats item %zu", i);
          pop(c);
          break;
        }
      }
    }
    return;
  }
  size_t cap = 32;
  while (cap < n * 2) cap <<= 1;
  uint32_t *slots = cilo_arena_alloc_zeroed(c->tmp, cap * sizeof(uint32_t), alignof(uint32_t));
  for (size_t j = 0; j < n; j++) {
    const char *v = list->items + j * size;
    for (size_t h = cima__value_hash(item, v) & (cap - 1);; h = (h + 1) & (cap - 1)) {
      if (!slots[h]) {
        slots[h] = (uint32_t)j + 1;
        break;
      }
      size_t i = slots[h] - 1;
      if (cima__value_eq(item, list->items + i * size, v)) {
        push_index(c, j);
        reportf(c, CIMA_ISSUE_NOT_UNIQUE, "repeats item %zu", i);
        pop(c);
        break;
      }
    }
  }
}

/* ========================================================================================================== */
/* building values.                                                                                           */
/* ========================================================================================================== */

static bool is_space(char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\v' || ch == '\f'; }

// `buf` holds `len` decoded bytes in a block of `cap` from the output allocator. trims, lowercases, terminates,
// gives back what is left and checks.
static void finish_string(cima_ctx *c, const cima_schema *s, char *buf, size_t len, size_t cap, cima_string *dst) {
  if (s->string.trim) {
    size_t start = 0;
    while (start < len && is_space(buf[start])) start++;
    while (len > start && is_space(buf[len - 1])) len--;
    if (start) memmove(buf, buf + start, len - start);
    len -= start;
  }
  if (s->string.lowercase)
    for (size_t i = 0; i < len; i++)
      if (buf[i] >= 'A' && buf[i] <= 'Z') buf[i] = (char)(buf[i] + 32);
  buf[len] = '\0';
  if (len + 1 < cap && !c->out_is_heap) buf = cilo_realloc(c->out, buf, cap, len + 1, 1);
  *dst = buf;
  check_string(c, s, buf, len);
}

static void store_string(cima_ctx *c, const cima_schema *s, const char *text, size_t len, cima_string *dst) {
  if (memchr(text, '\0', len)) {
    report(c, CIMA_ISSUE_INVALID_FORMAT, "must not hold \\u0000");
    return;
  }
  char *buf = cilo_alloc(c->out, len + 1, 1);
  memcpy(buf, text, len);
  finish_string(c, s, buf, len, len + 1, dst);
}

static void store_bytes(cima_ctx *c, const cima_schema *s, const char *text, size_t len, cima_bytes *dst) {
  size_t cap = len / 4 * 3 + 3, n;
  uint8_t *buf = cilo_alloc(c->out, cap, 1);
  if (!cima__base64_decode(text, len, buf, &n)) {
    cilo_free(c->out, buf, cap);
    report(c, CIMA_ISSUE_INVALID_FORMAT, "must be valid base64");
    return;
  }
  if (n == 0) {
    cilo_free(c->out, buf, cap);
    buf = nullptr;
  } else if (n < cap && !c->out_is_heap) {
    buf = cilo_realloc(c->out, buf, cap, n, 1);
  }
  *dst = (cima_bytes){buf, n};
  check_bytes(c, s, n);
}

static void store_timestamp(cima_ctx *c, const cima_schema *s, const char *text, size_t len, int64_t *dst) {
  if (!cima__parse_timestamp(text, len, dst)) {
    report(c, CIMA_ISSUE_INVALID_FORMAT, "must be an iso 8601 date and time");
    return;
  }
  check_int(c, s, false, *dst, 0);
}

static void store_enum(cima_ctx *c, const cima_schema *s, const char *text, size_t len, void *dst) {
  for (size_t i = 0; i < s->names_count; i++) {
    if (strlen(s->names[i]) == len && memcmp(s->names[i], text, len) == 0) {
      cima__write_enum(dst, s->size, (int64_t)i);
      return;
    }
  }
  cima_json_writer w = {.alloc = CILO_OF(c->tmp)};
  cima__put(&w, "must be one of ", 15);
  for (size_t i = 0; i < s->names_count; i++) {
    if (i) cima__put(&w, ", ", 2);
    cima__put(&w, s->names[i], strlen(s->names[i]));
  }
  report(c, CIMA_ISSUE_INVALID_ENUM, cima_json_writer_finish(&w, nullptr));
}

static void int_limits(cima_kind k, uint64_t *neg, uint64_t *pos) {
  switch (k) {
  case CIMA_KIND_I8: *neg = 128, *pos = INT8_MAX; return;
  case CIMA_KIND_I16: *neg = 32768, *pos = INT16_MAX; return;
  case CIMA_KIND_I32: *neg = (uint64_t)INT32_MAX + 1, *pos = INT32_MAX; return;
  case CIMA_KIND_U8: *neg = 0, *pos = UINT8_MAX; return;
  case CIMA_KIND_U16: *neg = 0, *pos = UINT16_MAX; return;
  case CIMA_KIND_U32: *neg = 0, *pos = UINT32_MAX; return;
  case CIMA_KIND_U64: *neg = 0, *pos = UINT64_MAX; return;
  default: *neg = (uint64_t)INT64_MAX + 1, *pos = INT64_MAX; return;
  }
}

static void store_number(cima_ctx *c, const cima_schema *s, const cima__num *n, const char *tok, size_t len,
                         void *dst) {
  if (s->kind == CIMA_KIND_NUMBER) {
    double d = cima__num_double(n, tok, len);
    if (!isfinite(d)) {
      report(c, CIMA_ISSUE_TOO_BIG, "is out of the range of a double");
      return;
    }
    *(double *)dst = d;
    check_number(c, s, d);
    return;
  }
  uint64_t mag, neg_max, pos_max;
  int_limits(s->kind, &neg_max, &pos_max);
  cima__int_status st = cima__num_magnitude(n, &mag);
  if (st == CIMA__INT_FRACTION) {
    report(c, CIMA_ISSUE_INVALID_TYPE, "expected an integer, got a fraction");
    return;
  }
  if (n->neg && (st == CIMA__INT_RANGE || mag > neg_max)) {
    reportf(c, CIMA_ISSUE_TOO_SMALL, "must be at least %s%llu", neg_max ? "-" : "", (unsigned long long)neg_max);
    return;
  }
  if (!n->neg && (st == CIMA__INT_RANGE || mag > pos_max)) {
    reportf(c, CIMA_ISSUE_TOO_BIG, "must be at most %llu", (unsigned long long)pos_max);
    return;
  }
  int64_t i = n->neg ? (int64_t)(0 - mag) : (int64_t)mag;
  switch (s->kind) {
  case CIMA_KIND_I8: *(int8_t *)dst = (int8_t)i; break;
  case CIMA_KIND_I16: *(int16_t *)dst = (int16_t)i; break;
  case CIMA_KIND_I32: *(int32_t *)dst = (int32_t)i; break;
  case CIMA_KIND_U8: *(uint8_t *)dst = (uint8_t)mag; break;
  case CIMA_KIND_U16: *(uint16_t *)dst = (uint16_t)mag; break;
  case CIMA_KIND_U32: *(uint32_t *)dst = (uint32_t)mag; break;
  case CIMA_KIND_U64: *(uint64_t *)dst = mag; break;
  default: *(int64_t *)dst = i; break;
  }
  check_int(c, s, !n->neg && mag > INT64_MAX, i, mag);
}

// lists gather in the scratch arena, then take an exact block from the output allocator. the item being parsed
// counts already, so a list cut short by an error still releases what its items hold.
typedef struct list_builder {
  char *buf;
  size_t len, cap, size, align;
} list_builder;

static void *list_next(cima_ctx *c, list_builder *b) {
  if (UNLIKELY(b->len == b->cap)) {
    size_t cap = b->cap ? b->cap * 2 : 8;
    b->buf = cilo_arena_realloc(c->tmp, b->buf, b->cap * b->size, cap * b->size, b->align);
    b->cap = cap;
  }
  void *slot = b->buf + b->len++ * b->size;
  memset(slot, 0, b->size);
  return slot;
}

static void list_finish(cima_ctx *c, const cima_schema *s, list_builder *b, void *dst) {
  cima__list *list = dst;
  list->len = b->len;
  list->items = nullptr;
  if (b->len) {
    list->items = cilo_alloc(c->out, b->len * b->size, b->align);
    memcpy(list->items, b->buf, b->len * b->size);
  }
  if (cima__list_has_cap(s)) list->cap = b->len;
}

static void clone_default(cima_ctx *c, const cima_schema *s, const char *json, void *dst);

static const char *default_of(const cima_schema *s) {
  for (;; s = s->inner) {
    if (s->common.default_json) return s->common.default_json;
    if (s->kind != CIMA_KIND_REF) return nullptr;
  }
}

static void missing(cima_ctx *c, const cima_schema *s, const char *key, size_t len, void *dst) {
  const char *json = default_of(s);
  if (json) {
    push_key(c, key, len);
    clone_default(c, s, json, dst);
    pop(c);
    return;
  }
  if (s->kind == CIMA_KIND_OPTIONAL) return;
  push_key(c, key, len);
  report(c, CIMA_ISSUE_REQUIRED, "required");
  pop(c);
}

/* ========================================================================================================== */
/* json.                                                                                                      */
/* ========================================================================================================== */

static const char *got(char ch) {
  switch (ch) {
  case '"': return "a string";
  case '{': return "an object";
  case '[': return "an array";
  case 't':
  case 'f': return "a boolean";
  case 'n': return "null";
  default: return "a number";
  }
}

static void skip_value(cima_ctx *c) {
  cima__lex_error err;
  const char *q = cima__skip(c->p, c->end, c->max_depth > c->depth ? c->max_depth - c->depth : 0, &err);
  if (!q) {
    syntax(c, err.at, err.msg);
    return;
  }
  c->p = q;
}

static void mismatch(cima_ctx *c, const char *expected) {
  reportf(c, CIMA_ISSUE_INVALID_TYPE, "expected %s, got %s", expected, got(*c->p));
  skip_value(c);
}

static bool scan_str(cima_ctx *c, cima__str *s) {
  const char *err;
  const char *q = cima__scan_string(c->p, c->end, s, &err);
  if (!q) {
    syntax(c, c->p, err);
    return false;
  }
  c->p = q;
  return true;
}

// the decoded text of a scanned string, in scratch when it had escapes.
static const char *str_text(cima_ctx *c, const cima__str *s, size_t *len) {
  if (!s->escaped) {
    *len = s->len;
    return s->ptr;
  }
  char *buf = cilo_arena_alloc(c->tmp, s->len + 1, 1);
  *len = cima__unescape(buf, s->ptr, s->len);
  buf[*len] = '\0';
  return buf;
}

static bool enter(cima_ctx *c) {
  if (++c->depth > c->max_depth) {
    syntax(c, c->p, "nested too deep");
    return false;
  }
  return true;
}

static void parse_value(cima_ctx *c, const cima_schema *s, void *out);
static void from_text(cima_ctx *c, const cima_schema *s, const char *t, size_t len, void *out);
static void text_value(cima_ctx *c, const cima_schema *s, const char *t, size_t len, void *out);

static void parse_array(cima_ctx *c, const cima_schema *s, void *out) {
  if (!enter(c)) return;
  const cima_schema *item = s->inner;
  list_builder b = {.size = item->size, .align = item->align};
  c->p = cima__ws(c->p + 1, c->end);
  if (c->p < c->end && *c->p == ']') {
    c->p++;
  } else {
    for (;;) {
      void *slot = list_next(c, &b);
      push_index(c, b.len - 1);
      parse_value(c, item, slot);
      pop(c);
      if (c->stop) break;
      c->p = cima__ws(c->p, c->end);
      if (c->p < c->end && *c->p == ',') {
        c->p++;
        continue;
      }
      if (c->p < c->end && *c->p == ']') {
        c->p++;
        break;
      }
      syntax(c, c->p, "expected ',' or ']'");
      break;
    }
  }
  c->depth--;
  list_finish(c, s, &b, out);
  if (!c->stop) check_list(c, s, out);
}

// `skip` names a key that belongs to someone else, the tag of a tagged union.
static void parse_object(cima_ctx *c, const cima_schema *s, void *out, const char *skip, size_t skip_len) {
  size_t before = c->nissues;
  if (!enter(c)) return;
  const cima__object_info *info = cima__object_info_of(s);
  size_t n = s->fields_count;
  uint64_t small = 0, *seen = n <= 64 ? &small : cilo_arena_alloc_zeroed(c->tmp, (n + 63) / 64 * 8, 8);
  size_t hint = 0;
  bool strict = s->object.unknown_keys == CIMA_UNKNOWN_KEYS_REJECT;
  c->p = cima__ws(c->p + 1, c->end);
  if (c->p < c->end && *c->p == '}') {
    c->p++;
  } else {
    for (;;) {
      if (c->p >= c->end || *c->p != '"') {
        syntax(c, c->p, "expected a key");
        break;
      }
      cima__str k;
      if (!scan_str(c, &k)) break;
      size_t klen;
      const char *key = str_text(c, &k, &klen);
      c->p = cima__ws(c->p, c->end);
      if (c->p >= c->end || *c->p != ':') {
        syntax(c, c->p, "expected ':'");
        break;
      }
      c->p = cima__ws(c->p + 1, c->end);
      if (c->p >= c->end) {
        syntax(c, c->p, "unexpected end of input");
        break;
      }
      ptrdiff_t i = skip && klen == skip_len && memcmp(key, skip, klen) == 0 ? -2 : cima__find_field(info, key, klen, hint);
      if (i >= 0 && !(seen[i / 64] & (1ull << (i % 64)))) {
        seen[i / 64] |= 1ull << (i % 64);
        hint = (size_t)i + 1;
        push_key(c, info->names[i], info->lens[i]);
        parse_value(c, s->fields[i].schema, (char *)out + s->fields[i].offset);
        pop(c);
      } else {
        if (i >= 0 || (i == -1 && strict)) {
          push_key(c, key, klen);
          report(c, i >= 0 ? CIMA_ISSUE_DUPLICATE_KEY : CIMA_ISSUE_UNKNOWN_KEY, i >= 0 ? "duplicate key" : "unknown key");
          pop(c);
        }
        skip_value(c);
      }
      if (c->stop) break;
      c->p = cima__ws(c->p, c->end);
      if (c->p < c->end && *c->p == ',') {
        c->p = cima__ws(c->p + 1, c->end);
        continue;
      }
      if (c->p < c->end && *c->p == '}') {
        c->p++;
        break;
      }
      syntax(c, c->p, "expected ',' or '}'");
      break;
    }
  }
  c->depth--;
  if (c->stop) return;
  for (size_t i = 0; i < n; i++)
    if (!(seen[i / 64] & (1ull << (i % 64))))
      missing(c, s->fields[i].schema, info->names[i], info->lens[i], (char *)out + s->fields[i].offset);
  refine(c, s, out, before);
}

static void parse_tagged(cima_ctx *c, const cima_schema *s, void *out) {
  const char *start = c->p;
  size_t tag_len = strlen(s->tag);
  cima__str tag = {};
  bool found = false, typed = true;
  const char *p = cima__ws(start + 1, c->end);
  cima__lex_error err = {};
  // the tag usually comes first, then this loop runs once.
  while (p < c->end && *p == '"') {
    cima__str k;
    const char *q = cima__scan_string(p, c->end, &k, &err.msg);
    if (!q) {
      syntax(c, p, err.msg);
      return;
    }
    size_t klen;
    const char *key = str_text(c, &k, &klen);
    p = cima__ws(q, c->end);
    if (p >= c->end || *p != ':') {
      syntax(c, p, "expected ':'");
      return;
    }
    p = cima__ws(p + 1, c->end);
    if (klen == tag_len && memcmp(key, s->tag, klen) == 0) {
      found = true;
      typed = p < c->end && *p == '"';
      if (typed && !(q = cima__scan_string(p, c->end, &tag, &err.msg))) {
        syntax(c, p, err.msg);
        return;
      }
      break;
    }
    if (!(p = cima__skip(p, c->end, c->max_depth > c->depth ? c->max_depth - c->depth : 0, &err))) {
      syntax(c, err.at, err.msg);
      return;
    }
    p = cima__ws(p, c->end);
    if (p >= c->end || *p != ',') break;
    p = cima__ws(p + 1, c->end);
  }
  if (!found || !typed) {
    push_key(c, s->tag, tag_len);
    report(c, found ? CIMA_ISSUE_INVALID_TAG : CIMA_ISSUE_REQUIRED, found ? "expected a string" : "required");
    pop(c);
    skip_value(c);
    return;
  }
  size_t len;
  const char *name = str_text(c, &tag, &len);
  for (size_t i = 0; i < s->fields_count; i++) {
    const cima_field *v = &s->fields[i];
    if (strlen(v->name) != len || memcmp(v->name, name, len) != 0) continue;
    cima__write_enum((char *)out + s->tag_offset, CIMA__KIND_SIZE, (int64_t)i);
    const cima_schema *target = cima__deref(v->schema);
    if (target->kind != CIMA_KIND_OBJECT) {
      report(c, CIMA_ISSUE_INVALID_TYPE, "variants must be objects");
      skip_value(c);
      return;
    }
    parse_object(c, target, (char *)out + v->offset, s->tag, tag_len);
    return;
  }
  cima_json_writer w = {.alloc = CILO_OF(c->tmp)};
  cima__put(&w, "must be one of ", 15);
  for (size_t i = 0; i < s->fields_count; i++) {
    if (i) cima__put(&w, ", ", 2);
    cima__put(&w, s->fields[i].name, strlen(s->fields[i].name));
  }
  push_key(c, s->tag, tag_len);
  report(c, CIMA_ISSUE_INVALID_TAG, cima_json_writer_finish(&w, nullptr));
  pop(c);
  skip_value(c);
}

static const char *literal(cima_ctx *c, const char *word, size_t n) {
  if ((size_t)(c->end - c->p) < n || memcmp(c->p, word, n) != 0) {
    syntax(c, c->p, "invalid literal");
    return nullptr;
  }
  return c->p + n;
}

static void parse_value(cima_ctx *c, const cima_schema *s, void *out) {
  s = cima__deref(s);
  c->p = cima__ws(c->p, c->end);
  if (c->p >= c->end) {
    syntax(c, c->p, "unexpected end of input");
    return;
  }
  size_t before = c->nissues;
  char ch = *c->p;
  cima__str str;
  size_t len;
  const char *text;
  if (ch == '"' && c->coerce && (s->kind <= CIMA_KIND_NUMBER)) {
    if (!scan_str(c, &str)) return;
    text = str_text(c, &str, &len);
    from_text(c, s, text, len, out);
    refine(c, s, out, before);
    return;
  }
  switch (s->kind) {
  case CIMA_KIND_BOOL:
    if (ch == 't' || ch == 'f') {
      const char *q = literal(c, ch == 't' ? "true" : "false", ch == 't' ? 4 : 5);
      if (!q) return;
      *(bool *)out = ch == 't';
      c->p = q;
    } else {
      mismatch(c, "a boolean");
      return;
    }
    break;
  case CIMA_KIND_I8:
  case CIMA_KIND_I16:
  case CIMA_KIND_I32:
  case CIMA_KIND_I64:
  case CIMA_KIND_U8:
  case CIMA_KIND_U16:
  case CIMA_KIND_U32:
  case CIMA_KIND_U64:
  case CIMA_KIND_NUMBER: {
    if (ch != '-' && (ch < '0' || ch > '9')) {
      mismatch(c, s->kind == CIMA_KIND_NUMBER ? "a number" : "an integer");
      return;
    }
    cima__num n;
    const char *q = cima__scan_number(c->p, c->end, &n);
    if (!q) {
      syntax(c, c->p, "invalid number");
      return;
    }
    store_number(c, s, &n, c->p, (size_t)(q - c->p), out);
    c->p = q;
    break;
  }
  case CIMA_KIND_STRING:
  case CIMA_KIND_BYTES:
  case CIMA_KIND_TIMESTAMP:
  case CIMA_KIND_ENUM:
    if (ch != '"') {
      mismatch(c, "a string");
      return;
    }
    if (!scan_str(c, &str)) return;
    if (s->kind == CIMA_KIND_STRING) {
      size_t cap = str.len + 1;
      char *buf = cilo_alloc(c->out, cap, 1);
      if (str.escaped) {
        len = cima__unescape(buf, str.ptr, str.len);
        if (memchr(buf, '\0', len)) {
          cilo_free(c->out, buf, cap);
          report(c, CIMA_ISSUE_INVALID_FORMAT, "must not hold \\u0000");
          return;
        }
      } else {
        memcpy(buf, str.ptr, len = str.len);
      }
      finish_string(c, s, buf, len, cap, out);
      break;
    }
    text = str_text(c, &str, &len);
    if (s->kind == CIMA_KIND_BYTES) store_bytes(c, s, text, len, out);
    else if (s->kind == CIMA_KIND_TIMESTAMP) store_timestamp(c, s, text, len, out);
    else store_enum(c, s, text, len, out);
    break;
  case CIMA_KIND_UNKNOWN: {
    const char *start = c->p;
    skip_value(c);
    if (c->stop) return;
    size_t n = (size_t)(c->p - start);
    char *raw = cilo_alloc(c->out, n + 1, 1);
    memcpy(raw, start, n);
    raw[n] = '\0';
    *(cima_unknown *)out = raw;
    break;
  }
  case CIMA_KIND_ARRAY:
    if (ch != '[') {
      mismatch(c, "an array");
      return;
    }
    parse_array(c, s, out);
    break;
  case CIMA_KIND_OPTIONAL:
    *(bool *)out = true;
    parse_value(c, s->inner, (char *)out + s->value_offset);
    return;
  case CIMA_KIND_NULLABLE:
    if (ch == 'n') {
      const char *q = literal(c, "null", 4);
      if (q) c->p = q;
      return;
    }
    *(bool *)out = true;
    parse_value(c, s->inner, (char *)out + s->value_offset);
    return;
  case CIMA_KIND_OBJECT:
    if (ch != '{') {
      mismatch(c, "an object");
      return;
    }
    parse_object(c, s, out, nullptr, 0);
    return;
  case CIMA_KIND_TAGGED_UNION:
    if (ch != '{') {
      mismatch(c, "an object");
      return;
    }
    parse_tagged(c, s, out);
    break;
  case CIMA_KIND_REF: break;
  }
  refine(c, s, out, before);
}

/* ========================================================================================================== */
/* text: path segments, headers, query strings and forms.                                                     */
/* ========================================================================================================== */

static bool word_is(const char *t, size_t len, const char *word) { return strlen(word) == len && memcmp(t, word, len) == 0; }

static bool stringish(const cima_schema *s) {
  s = cima__deref(s);
  return s->kind == CIMA_KIND_STRING || s->kind == CIMA_KIND_UNKNOWN;
}

// json text in the middle of other text: an object in a query parameter, for instance.
static void embedded_json(cima_ctx *c, const cima_schema *s, const char *t, size_t len, void *out) {
  const char *start = c->start, *p = c->p, *end = c->end;
  c->start = c->p = t;
  c->end = t + len;
  parse_value(c, s, out);
  const char *rest = cima__ws(c->p, c->end);
  if (!c->stop && rest != c->end) syntax(c, rest, "trailing characters");
  c->start = start;
  c->p = p;
  c->end = end;
}

static void from_text(cima_ctx *c, const cima_schema *s, const char *t, size_t len, void *out) {
  s = cima__deref(s);
  switch (s->kind) {
  case CIMA_KIND_BOOL:
    if (word_is(t, len, "true") || word_is(t, len, "1") || word_is(t, len, "on") || word_is(t, len, "yes"))
      *(bool *)out = true;
    else if (word_is(t, len, "false") || word_is(t, len, "0") || word_is(t, len, "off") || word_is(t, len, "no"))
      *(bool *)out = false;
    else report(c, CIMA_ISSUE_INVALID_TYPE, "expected a boolean");
    return;
  case CIMA_KIND_I8:
  case CIMA_KIND_I16:
  case CIMA_KIND_I32:
  case CIMA_KIND_I64:
  case CIMA_KIND_U8:
  case CIMA_KIND_U16:
  case CIMA_KIND_U32:
  case CIMA_KIND_U64:
  case CIMA_KIND_NUMBER: {
    cima__num n;
    const char *q = cima__scan_number(t, t + len, &n);
    if (!q || q != t + len) {
      report(c, CIMA_ISSUE_INVALID_TYPE, s->kind == CIMA_KIND_NUMBER ? "expected a number" : "expected an integer");
      return;
    }
    store_number(c, s, &n, t, len, out);
    return;
  }
  case CIMA_KIND_STRING: store_string(c, s, t, len, out); return;
  case CIMA_KIND_BYTES: store_bytes(c, s, t, len, out); return;
  case CIMA_KIND_TIMESTAMP: store_timestamp(c, s, t, len, out); return;
  case CIMA_KIND_ENUM: store_enum(c, s, t, len, out); return;
  case CIMA_KIND_UNKNOWN: {
    cima_json_writer w = {.alloc = c->out};
    if (cima_json_valid(t, len)) cima__put(&w, t, len);
    else cima__write_quoted(&w, t, len);
    *(cima_unknown *)out = cima_json_writer_finish(&w, nullptr);
    return;
  }
  case CIMA_KIND_OPTIONAL:
  case CIMA_KIND_NULLABLE:
    if ((s->kind == CIMA_KIND_NULLABLE && word_is(t, len, "null")) || (len == 0 && !stringish(s->inner))) return;
    *(bool *)out = true;
    text_value(c, s->inner, t, len, (char *)out + s->value_offset);
    return;
  case CIMA_KIND_ARRAY: {
    // comma separated, as headers repeat themselves.
    list_builder b = {.size = s->inner->size, .align = s->inner->align};
    const char *end = t + len;
    for (const char *p = t; len && p <= end && !c->stop;) {
      const char *comma = memchr(p, ',', (size_t)(end - p));
      if (!comma) comma = end;
      const char *a = p, *z = comma;
      while (a < z && is_space(*a)) a++;
      while (z > a && is_space(z[-1])) z--;
      void *slot = list_next(c, &b);
      push_index(c, b.len - 1);
      text_value(c, s->inner, a, (size_t)(z - a), slot);
      pop(c);
      p = comma + 1;
    }
    list_finish(c, s, &b, out);
    if (!c->stop) check_list(c, s, out);
    return;
  }
  case CIMA_KIND_OBJECT:
  case CIMA_KIND_TAGGED_UNION: {
    const char *p = t;
    while (p < t + len && is_space(*p)) p++;
    if (p < t + len && *p == '{') embedded_json(c, s, t, len, out);
    else report(c, CIMA_ISSUE_INVALID_TYPE, "expected an object");
    return;
  }
  case CIMA_KIND_REF: return;
  }
}

static void text_value(cima_ctx *c, const cima_schema *s, const char *t, size_t len, void *out) {
  size_t before = c->nissues;
  s = cima__deref(s);
  from_text(c, s, t, len, out);
  if (s->kind != CIMA_KIND_OBJECT && s->kind != CIMA_KIND_TAGGED_UNION) refine(c, s, out, before);
}

static void pairs_object(cima_ctx *c, const cima_schema *s, const cima_pair *pairs, size_t count, void *out) {
  size_t before = c->nissues;
  const cima__object_info *info = cima__object_info_of(s);
  size_t n = s->fields_count;
  bool strict = s->object.unknown_keys == CIMA_UNKNOWN_KEYS_REJECT;
  int32_t *field_of = cilo_arena_alloc(c->tmp, (count + 1) * sizeof(int32_t), alignof(int32_t));
  size_t *hits = cilo_arena_alloc_zeroed(c->tmp, (n + 1) * sizeof(size_t), alignof(size_t));
  for (size_t i = 0; i < count; i++) {
    const char *key = pairs[i].key ? pairs[i].key : "";
    size_t klen = strlen(key);
    // `tags[]=a&tags[]=b` names the same list as `tags=a&tags=b`.
    if (klen > 2 && key[klen - 2] == '[' && key[klen - 1] == ']') klen -= 2;
    ptrdiff_t f = cima__find_field(info, key, klen, 0);
    field_of[i] = (int32_t)f;
    if (f >= 0) hits[f]++;
    else if (strict) {
      push_key(c, key, klen);
      report(c, CIMA_ISSUE_UNKNOWN_KEY, "unknown key");
      pop(c);
    }
  }
  for (size_t f = 0; f < n && !c->stop; f++) {
    const cima_schema *fs = s->fields[f].schema;
    void *dst = (char *)out + s->fields[f].offset;
    const cima_schema *list = cima__deref(fs);
    bool optional = list->kind == CIMA_KIND_OPTIONAL;
    if (optional) list = cima__deref(list->inner);
    if (!hits[f]) {
      missing(c, fs, info->names[f], info->lens[f], dst);
      continue;
    }
    push_key(c, info->names[f], info->lens[f]);
    if (list->kind == CIMA_KIND_ARRAY) {
      size_t list_before = c->nissues;
      if (optional) {
        *(bool *)dst = true;
        dst = (char *)dst + cima__deref(fs)->value_offset;
      }
      list_builder b = {.size = list->inner->size, .align = list->inner->align};
      for (size_t i = 0; i < count && !c->stop; i++) {
        if (field_of[i] != (int32_t)f) continue;
        void *slot = list_next(c, &b);
        push_index(c, b.len - 1);
        const char *v = pairs[i].value ? pairs[i].value : "";
        text_value(c, list->inner, v, strlen(v), slot);
        pop(c);
      }
      list_finish(c, list, &b, dst);
      if (!c->stop) check_list(c, list, dst);
      refine(c, list, dst, list_before);
    } else if (hits[f] > 1) {
      report(c, CIMA_ISSUE_DUPLICATE_KEY, "expected a single value");
    } else {
      size_t i = 0;
      while (field_of[i] != (int32_t)f) i++;
      const char *v = pairs[i].value ? pairs[i].value : "";
      size_t len = strlen(v);
      // an empty form input means the field was left out, unless it holds text.
      if (c->coerce && len == 0 && !stringish(optional ? list : fs) && (optional || default_of(fs))) {
        pop(c);
        missing(c, fs, info->names[f], info->lens[f], dst);
        continue;
      }
      text_value(c, fs, v, len, dst);
    }
    pop(c);
  }
  refine(c, s, out, before);
}

/* ========================================================================================================== */
/* checking values built in c.                                                                                */
/* ========================================================================================================== */

static void check_value(cima_ctx *c, const cima_schema *s, const void *v) {
  s = cima__deref(s);
  size_t before = c->nissues;
  if (c->stop) return;
  if (++c->depth > c->max_depth) {
    report(c, CIMA_ISSUE_TOO_DEEP, "nested too deep");
    c->depth--;
    return;
  }
  switch (s->kind) {
  case CIMA_KIND_BOOL: break;
  case CIMA_KIND_I8:
  case CIMA_KIND_I16:
  case CIMA_KIND_I32:
  case CIMA_KIND_I64:
  case CIMA_KIND_U8:
  case CIMA_KIND_U16:
  case CIMA_KIND_U32:
  case CIMA_KIND_TIMESTAMP: check_int(c, s, false, cima__read_int(s->kind, v), 0); break;
  case CIMA_KIND_U64: {
    uint64_t u = *(const uint64_t *)v;
    check_int(c, s, u > INT64_MAX, (int64_t)u, u);
    break;
  }
  case CIMA_KIND_NUMBER: {
    double d = *(const double *)v;
    if (!isfinite(d)) report(c, CIMA_ISSUE_INVALID_TYPE, "must be a finite number");
    else check_number(c, s, d);
    break;
  }
  case CIMA_KIND_STRING: {
    const char *str = *(const cima_string *)v;
    if (!str) report(c, CIMA_ISSUE_REQUIRED, "required");
    else check_string(c, s, str, strlen(str));
    break;
  }
  case CIMA_KIND_BYTES: check_bytes(c, s, ((const cima_bytes *)v)->len); break;
  case CIMA_KIND_UNKNOWN: {
    const char *raw = *(const cima_unknown *)v;
    if (!raw) report(c, CIMA_ISSUE_REQUIRED, "required");
    else if (!cima_json_valid(raw, strlen(raw))) report(c, CIMA_ISSUE_INVALID_JSON, "must be valid json");
    break;
  }
  case CIMA_KIND_ARRAY: {
    const cima__list *list = v;
    for (size_t i = 0; i < list->len && !c->stop; i++) {
      push_index(c, i);
      check_value(c, s->inner, list->items + i * s->inner->size);
      pop(c);
    }
    check_list(c, s, list);
    break;
  }
  case CIMA_KIND_OPTIONAL:
  case CIMA_KIND_NULLABLE:
    if (*(const bool *)v) check_value(c, s->inner, (const char *)v + s->value_offset);
    break;
  case CIMA_KIND_OBJECT: {
    const cima__object_info *info = cima__object_info_of(s);
    for (size_t i = 0; i < s->fields_count && !c->stop; i++) {
      push_key(c, info->names[i], info->lens[i]);
      check_value(c, s->fields[i].schema, (const char *)v + s->fields[i].offset);
      pop(c);
    }
    break;
  }
  case CIMA_KIND_ENUM: {
    int64_t i = cima__read_enum(v, s->size);
    if (i < 0 || (size_t)i >= s->names_count) report(c, CIMA_ISSUE_INVALID_ENUM, "is not a value of the enum");
    break;
  }
  case CIMA_KIND_TAGGED_UNION: {
    int64_t i = cima__read_enum((const char *)v + s->tag_offset, CIMA__KIND_SIZE);
    if (i < 0 || (size_t)i >= s->fields_count) {
      push_key(c, s->tag, strlen(s->tag));
      report(c, CIMA_ISSUE_INVALID_TAG, "is not a variant of the union");
      pop(c);
    } else {
      check_value(c, s->fields[i].schema, (const char *)v + s->fields[i].offset);
    }
    break;
  }
  case CIMA_KIND_REF: break;
  }
  c->depth--;
  refine(c, s, v, before);
}

/* ========================================================================================================== */
/* entry points.                                                                                              */
/* ========================================================================================================== */

typedef struct run {
  cima_ctx c;
  cilo_arena *arena;
  cilo_allocator *alloc;
  cilo_mark tmp_mark;
  cilo_mark out_mark;
} run;

static void begin(run *r, const cima_options *o) {
  static const cima_options none = {};
  if (!o) o = &none;
  cilo_arena *arena = o->arena;
  if (!arena && o->alloc && !*o->alloc) arena = (cilo_arena *)o->alloc;
  r->arena = arena;
  r->alloc = arena ? CILO_OF(arena) : o->alloc ? o->alloc : cima__heap();
  cilo_arena *tmp = cilo__scratch(&arena, arena ? 1 : 0);
  r->tmp_mark = cilo_mark_of(tmp);
  if (arena) r->out_mark = cilo_mark_of(arena);
  r->c = (cima_ctx){
      .out = r->alloc,
      .out_is_heap = r->alloc == cima__heap(),
      .tmp = tmp,
      .coerce = o->coerce,
      .max_depth = o->max_depth ? o->max_depth : 64,
      .max_issues = o->max_issues,
  };
}

static cima_issues copy_issues(run *r, cima_result *res) {
  cima_ctx *c = &r->c;
  size_t total = c->nissues * sizeof(cima_issue);
  for (size_t i = 0; i < c->nissues; i++)
    total += strlen(c->issues[i].path) + strlen(c->issues[i].message) + 2;
  cima_issue *items = cilo_alloc(r->alloc, total, alignof(cima_issue));
  char *text = (char *)(items + c->nissues);
  for (size_t i = 0; i < c->nissues; i++) {
    size_t p = strlen(c->issues[i].path) + 1, m = strlen(c->issues[i].message) + 1;
    items[i] = (cima_issue){.path = memcpy(text, c->issues[i].path, p), .code = c->issues[i].code};
    items[i].message = memcpy(text + p, c->issues[i].message, m);
    text += p + m;
  }
  if (!r->arena) {
    res->owner = r->alloc;
    res->owned = total;
  }
  return (cima_issues){items, c->nissues};
}

static cima_result finish(run *r, const cima_schema *schema, void *out) {
  cima_result res = {.ok = r->c.nissues == 0};
  if (!res.ok) {
    if (out) {
      // the arena gives back all the parse took at once, other allocators walk the value.
      if (r->arena) cilo_rewind(r->out_mark);
      else cima_dispose_with(schema, out, r->alloc);
      memset(out, 0, schema->size);
    }
    res.issues = copy_issues(r, &res);
  }
  cilo_rewind(r->tmp_mark);
  return res;
}

cima_result cima_parse_json(const cima_schema *schema, const char *json, size_t len, void *out,
                            const cima_options *options) {
  run r;
  begin(&r, options);
  memset(out, 0, schema->size);
  cima_ctx *c = &r.c;
  c->start = c->p = json ? json : "";
  c->end = c->p + (json ? len : 0);
  if (cima__ws(c->p, c->end) == c->end) {
    report(c, CIMA_ISSUE_INVALID_JSON, "empty input");
  } else {
    parse_value(c, schema, out);
    const char *rest = cima__ws(c->p, c->end);
    if (!c->stop && rest != c->end) syntax(c, rest, "trailing characters");
  }
  return finish(&r, schema, out);
}

cima_result cima_parse_pairs(const cima_schema *schema, const cima_pair *pairs, size_t count, void *out,
                             const cima_options *options) {
  run r;
  begin(&r, options);
  memset(out, 0, schema->size);
  const cima_schema *s = cima__deref(schema);
  if (s->kind != CIMA_KIND_OBJECT) report(&r.c, CIMA_ISSUE_INVALID_TYPE, "pairs need an object schema");
  else pairs_object(&r.c, s, pairs, count, out);
  return finish(&r, schema, out);
}

cima_result cima_parse_text(const cima_schema *schema, const char *text, void *out, const cima_options *options) {
  run r;
  begin(&r, options);
  memset(out, 0, schema->size);
  if (!text) text = "";
  text_value(&r.c, schema, text, strlen(text), out);
  return finish(&r, schema, out);
}

cima_result cima_check(const cima_schema *schema, const void *value, const cima_options *options) {
  run r;
  begin(&r, options);
  check_value(&r.c, schema, value);
  return finish(&r, schema, nullptr);
}

void cima_result_dispose(cima_result *result) {
  if (result->owner && result->issues.items) cilo_free(result->owner, (void *)result->issues.items, result->owned);
  *result = (cima_result){.ok = result->ok};
}

/* ========================================================================================================== */
/* defaults, parsed once per schema and copied into each value that needs them.                               */
/* ========================================================================================================== */

typedef struct cached_default {
  cima_kind kind;
  size_t size;
  bool ok;
  void *value;
  cilo_arena arena;
} cached_default;

static const cached_default *default_for(const cima_schema *s, const char *json) {
  cached_default *d = cima__cache_get(s, json, CIMA__TAG_DEFAULT);
  if (LIKELY(d && d->kind == s->kind && d->size == s->size)) return d;
  bool stale = d != nullptr;
  d = calloc(1, sizeof *d);
  if (!d) cilo_oom(sizeof *d);
  d->kind = s->kind;
  d->size = s->size;
  d->value = cilo_arena_alloc_zeroed(&d->arena, s->size ? s->size : 1, s->align ? s->align : 1);
  cima_result r = cima_parse_json(s, json, strlen(json), d->value, &(cima_options){.arena = &d->arena});
  d->ok = r.ok;
  cached_default *won = cima__cache_put(s, json, CIMA__TAG_DEFAULT, d, stale);
  if (won != d) {
    cilo_arena_drop(&d->arena);
    free(d);
  }
  return won;
}

static void clone_default(cima_ctx *c, const cima_schema *s, const char *json, void *dst) {
  const cached_default *d = default_for(s, json);
  if (!d->ok) {
    reportf(c, CIMA_ISSUE_CUSTOM, "the default %s does not fit the schema", json);
    return;
  }
  cima__clone_value(s, dst, d->value, c->out);
}

/* ========================================================================================================== */
/* values.                                                                                                    */
/* ========================================================================================================== */

void cima__clone_value(const cima_schema *s, void *dst, const void *src, cilo_allocator *a) {
  s = cima__deref(s);
  memcpy(dst, src, s->size);
  switch (s->kind) {
  case CIMA_KIND_STRING:
  case CIMA_KIND_UNKNOWN: {
    const char *str = *(const char *const *)src;
    if (str) *(char **)dst = cilo_strdup(a, str);
    return;
  }
  case CIMA_KIND_BYTES: {
    const cima_bytes *b = src;
    if (b->len) ((cima_bytes *)dst)->ptr = memcpy(cilo_alloc(a, b->len, 1), b->ptr, b->len);
    return;
  }
  case CIMA_KIND_ARRAY: {
    const cima__list *from = src;
    cima__list *to = dst;
    size_t size = s->inner->size;
    to->items = from->len ? cilo_alloc(a, from->len * size, s->inner->align) : nullptr;
    for (size_t i = 0; i < from->len; i++) cima__clone_value(s->inner, to->items + i * size, from->items + i * size, a);
    if (cima__list_has_cap(s)) to->cap = from->len;
    return;
  }
  case CIMA_KIND_OPTIONAL:
  case CIMA_KIND_NULLABLE:
    if (*(const bool *)src)
      cima__clone_value(s->inner, (char *)dst + s->value_offset, (const char *)src + s->value_offset, a);
    return;
  case CIMA_KIND_OBJECT:
    for (size_t i = 0; i < s->fields_count; i++)
      cima__clone_value(s->fields[i].schema, (char *)dst + s->fields[i].offset, (const char *)src + s->fields[i].offset,
                        a);
    return;
  case CIMA_KIND_TAGGED_UNION: {
    int64_t i = cima__read_enum((const char *)src + s->tag_offset, CIMA__KIND_SIZE);
    if (i >= 0 && (size_t)i < s->fields_count)
      cima__clone_value(s->fields[i].schema, (char *)dst + s->fields[i].offset, (const char *)src + s->fields[i].offset,
                        a);
    return;
  }
  default: return;
  }
}

bool cima__value_eq(const cima_schema *s, const void *a, const void *b) {
  s = cima__deref(s);
  switch (s->kind) {
  case CIMA_KIND_BOOL: return *(const bool *)a == *(const bool *)b;
  case CIMA_KIND_NUMBER: return *(const double *)a == *(const double *)b;
  case CIMA_KIND_STRING:
  case CIMA_KIND_UNKNOWN: {
    const char *x = *(const char *const *)a, *y = *(const char *const *)b;
    return x == y || (x && y && strcmp(x, y) == 0);
  }
  case CIMA_KIND_BYTES: {
    const cima_bytes *x = a, *y = b;
    return x->len == y->len && (!x->len || memcmp(x->ptr, y->ptr, x->len) == 0);
  }
  case CIMA_KIND_ARRAY: {
    const cima__list *x = a, *y = b;
    if (x->len != y->len) return false;
    for (size_t i = 0; i < x->len; i++)
      if (!cima__value_eq(s->inner, x->items + i * s->inner->size, y->items + i * s->inner->size)) return false;
    return true;
  }
  case CIMA_KIND_OPTIONAL:
  case CIMA_KIND_NULLABLE: {
    bool x = *(const bool *)a, y = *(const bool *)b;
    return x == y && (!x || cima__value_eq(s->inner, (const char *)a + s->value_offset, (const char *)b + s->value_offset));
  }
  case CIMA_KIND_OBJECT:
    for (size_t i = 0; i < s->fields_count; i++) {
      size_t off = s->fields[i].offset;
      if (!cima__value_eq(s->fields[i].schema, (const char *)a + off, (const char *)b + off)) return false;
    }
    return true;
  case CIMA_KIND_ENUM: return cima__read_enum(a, s->size) == cima__read_enum(b, s->size);
  case CIMA_KIND_TAGGED_UNION: {
    int64_t x = cima__read_enum((const char *)a + s->tag_offset, CIMA__KIND_SIZE);
    int64_t y = cima__read_enum((const char *)b + s->tag_offset, CIMA__KIND_SIZE);
    if (x != y) return false;
    if (x < 0 || (size_t)x >= s->fields_count) return true;
    size_t off = s->fields[x].offset;
    return cima__value_eq(s->fields[x].schema, (const char *)a + off, (const char *)b + off);
  }
  default: return memcmp(a, b, s->size) == 0;
  }
}

static uint64_t mix(uint64_t h, uint64_t v) {
  h = (h ^ v) * 0x9e3779b97f4a7c15ull;
  return h ^ (h >> 32);
}

uint64_t cima__value_hash(const cima_schema *s, const void *v) {
  s = cima__deref(s);
  switch (s->kind) {
  case CIMA_KIND_NUMBER: {
    double d = *(const double *)v;
    if (d == 0) d = 0;
    uint64_t bits;
    memcpy(&bits, &d, 8);
    return mix(1, bits);
  }
  case CIMA_KIND_BOOL: return mix(2, *(const bool *)v);
  case CIMA_KIND_STRING:
  case CIMA_KIND_UNKNOWN: {
    const char *str = *(const char *const *)v;
    return str ? cima__hash(str, strlen(str)) : 3;
  }
  case CIMA_KIND_BYTES: {
    const cima_bytes *b = v;
    return b->len ? cima__hash(b->ptr, b->len) : 4;
  }
  case CIMA_KIND_ARRAY: {
    const cima__list *l = v;
    uint64_t h = mix(5, l->len);
    for (size_t i = 0; i < l->len; i++) h = mix(h, cima__value_hash(s->inner, l->items + i * s->inner->size));
    return h;
  }
  case CIMA_KIND_OPTIONAL:
  case CIMA_KIND_NULLABLE:
    return *(const bool *)v ? mix(6, cima__value_hash(s->inner, (const char *)v + s->value_offset)) : 7;
  case CIMA_KIND_OBJECT: {
    uint64_t h = 8;
    for (size_t i = 0; i < s->fields_count; i++)
      h = mix(h, cima__value_hash(s->fields[i].schema, (const char *)v + s->fields[i].offset));
    return h;
  }
  case CIMA_KIND_ENUM: return mix(9, (uint64_t)cima__read_enum(v, s->size));
  case CIMA_KIND_TAGGED_UNION: {
    int64_t k = cima__read_enum((const char *)v + s->tag_offset, CIMA__KIND_SIZE);
    if (k < 0 || (size_t)k >= s->fields_count) return 10;
    return mix((uint64_t)k, cima__value_hash(s->fields[k].schema, (const char *)v + s->fields[k].offset));
  }
  default: return mix(11, (uint64_t)cima__read_int(s->kind, v));
  }
}

static void dispose(const cima_schema *s, void *v, cilo_allocator *a) {
  s = cima__deref(s);
  switch (s->kind) {
  case CIMA_KIND_STRING:
  case CIMA_KIND_UNKNOWN: {
    char **str = v;
    if (*str) cilo_free(a, *str, strlen(*str) + 1);
    *str = nullptr;
    return;
  }
  case CIMA_KIND_BYTES: {
    cima_bytes *b = v;
    if (b->ptr) cilo_free(a, (void *)b->ptr, b->len);
    *b = (cima_bytes){};
    return;
  }
  case CIMA_KIND_ARRAY: {
    cima__list *l = v;
    bool cap = cima__list_has_cap(s);
    // a list pointing at items it does not own, a static array for instance, is left alone.
    if (cap && l->cap < l->len) return;
    for (size_t i = 0; i < l->len; i++) dispose(s->inner, l->items + i * s->inner->size, a);
    if (l->items) cilo_free(a, l->items, (cap ? l->cap : l->len) * s->inner->size);
    l->items = nullptr;
    l->len = 0;
    if (cap) l->cap = 0;
    return;
  }
  case CIMA_KIND_OPTIONAL:
  case CIMA_KIND_NULLABLE:
    if (*(bool *)v) dispose(s->inner, (char *)v + s->value_offset, a);
    *(bool *)v = false;
    return;
  case CIMA_KIND_OBJECT:
    for (size_t i = 0; i < s->fields_count; i++) dispose(s->fields[i].schema, (char *)v + s->fields[i].offset, a);
    return;
  case CIMA_KIND_TAGGED_UNION: {
    int64_t k = cima__read_enum((char *)v + s->tag_offset, CIMA__KIND_SIZE);
    if (k >= 0 && (size_t)k < s->fields_count) dispose(s->fields[k].schema, (char *)v + s->fields[k].offset, a);
    return;
  }
  default: return;
  }
}

void cima_dispose_with(const cima_schema *schema, void *value, cilo_allocator *alloc) {
  // arenas free everything at once.
  if (!*alloc) return;
  dispose(schema, value, alloc);
}

void cima_dispose(const cima_schema *schema, void *value) { cima_dispose_with(schema, value, cima__heap()); }

const cima_schema cima_bool_schema = {CIMA_INIT(CIMA_BOOLEAN())};
const cima_schema cima_i8_schema = {CIMA_INIT(CIMA_I8())};
const cima_schema cima_i16_schema = {CIMA_INIT(CIMA_I16())};
const cima_schema cima_i32_schema = {CIMA_INIT(CIMA_I32())};
const cima_schema cima_i64_schema = {CIMA_INIT(CIMA_I64())};
const cima_schema cima_u8_schema = {CIMA_INIT(CIMA_U8())};
const cima_schema cima_u16_schema = {CIMA_INIT(CIMA_U16())};
const cima_schema cima_u32_schema = {CIMA_INIT(CIMA_U32())};
const cima_schema cima_u64_schema = {CIMA_INIT(CIMA_U64())};
const cima_schema cima_integer_schema = {CIMA_INIT(CIMA_INTEGER())};
const cima_schema cima_number_schema = {CIMA_INIT(CIMA_NUMBER())};
const cima_schema cima_string_schema = {CIMA_INIT(CIMA_STRING())};
const cima_schema cima_bytes_schema = {CIMA_INIT(CIMA_BYTES())};
const cima_schema cima_timestamp_schema = {CIMA_INIT(CIMA_TIMESTAMP())};
const cima_schema cima_unknown_schema = {CIMA_INIT(CIMA_UNKNOWN())};
