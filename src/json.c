#include "internal.h"

#include <math.h>

/* ========================================================================================================== */
/* writer.                                                                                                    */
/* ========================================================================================================== */

void cima__writer_grow(cima_json_writer *w, size_t need) {
  cilo_allocator *a = w->alloc ? w->alloc : cima__heap();
  size_t want = w->cap ? w->cap * 2 : 1024;
  while (want - w->len < need) want *= 2;
  w->buf = cilo_realloc(a, w->buf, w->cap, want, 1);
  w->cap = want;
}

void cima__newline(cima_json_writer *w) {
  size_t n = (size_t)w->indent * w->depth;
  char *d = cima__reserve(w, n + 1);
  d[0] = '\n';
  memset(d + 1, ' ', n);
  w->len += n + 1;
}

// the end of the longest prefix that needs no escaping: printable ascii without quotes or backslashes, and valid
// utf-8 sequences.
static const char *clean_run(const char *p, const char *end) {
  for (;;) {
    while (end - p >= 8) {
      uint64_t m = cima__special(cima__load64(p)) & CIMA__HIGHS;
      if (m) {
        p += __builtin_ctzll(m) >> 3;
        goto found;
      }
      p += 8;
    }
    while (p < end && (unsigned char)*p >= 0x20 && *p != '"' && *p != '\\' && (unsigned char)*p < 0x80) p++;
  found:
    if (p < end && (unsigned char)*p >= 0x80) {
      size_t n = cima__utf8_seq((const unsigned char *)p, (const unsigned char *)end);
      if (n) {
        p += n;
        continue;
      }
    }
    return p;
  }
}

void cima__write_quoted(cima_json_writer *w, const char *s, size_t len) {
  char *d = cima__reserve(w, len + 2);
  const char *end = s + len;
  const char *q = clean_run(s, end);
  d[0] = '"';
  memcpy(d + 1, s, (size_t)(q - s));
  w->len += (size_t)(q - s) + 1;
  while (q < end) {
    unsigned char c = (unsigned char)*q++;
    switch (c) {
    case '"': cima__put(w, "\\\"", 2); break;
    case '\\': cima__put(w, "\\\\", 2); break;
    case '\n': cima__put(w, "\\n", 2); break;
    case '\r': cima__put(w, "\\r", 2); break;
    case '\t': cima__put(w, "\\t", 2); break;
    case '\b': cima__put(w, "\\b", 2); break;
    case '\f': cima__put(w, "\\f", 2); break;
    default:
      if (c >= 0x80) {
        cima__put(w, "\xef\xbf\xbd", 3);
      } else {
        char u[6] = {'\\', 'u', '0', '0', "0123456789abcdef"[c >> 4], "0123456789abcdef"[c & 15]};
        cima__put(w, u, 6);
      }
    }
    const char *r = clean_run(q, end);
    cima__put(w, q, (size_t)(r - q));
    q = r;
  }
  cima__putc(w, '"');
}

void cima_json_write_null(cima_json_writer *w) {
  cima__pre(w);
  cima__put(w, "null", 4);
  w->comma = true;
}

void cima_json_write_bool(cima_json_writer *w, bool value) {
  cima__pre(w);
  if (value) cima__put(w, "true", 4);
  else cima__put(w, "false", 5);
  w->comma = true;
}

void cima_json_write_int(cima_json_writer *w, int64_t value) {
  cima__pre(w);
  char *d = cima__reserve(w, 21);
  size_t n = 0;
  uint64_t u = (uint64_t)value;
  if (value < 0) {
    d[n++] = '-';
    u = 0 - u;
  }
  w->len += n + cima__fmt_u64(d + n, u);
  w->comma = true;
}

void cima_json_write_uint(cima_json_writer *w, uint64_t value) {
  cima__pre(w);
  w->len += cima__fmt_u64(cima__reserve(w, 20), value);
  w->comma = true;
}

void cima_json_write_double(cima_json_writer *w, double value) {
  if (!isfinite(value)) {
    cima_json_write_null(w);
    return;
  }
  cima__pre(w);
  w->len += cima__fmt_double(cima__reserve(w, 40), value);
  w->comma = true;
}

void cima_json_write_string_n(cima_json_writer *w, const char *s, size_t len) {
  cima__pre(w);
  cima__write_quoted(w, s, len);
  w->comma = true;
}

void cima_json_write_string(cima_json_writer *w, const char *s) {
  if (!s) cima_json_write_null(w);
  else cima_json_write_string_n(w, s, strlen(s));
}

void cima_json_write_raw(cima_json_writer *w, const char *json, size_t len) {
  cima__pre(w);
  cima__put(w, json, len);
  w->comma = true;
}

void cima_json_write_key_n(cima_json_writer *w, const char *key, size_t len) {
  cima__pre(w);
  cima__write_quoted(w, key, len);
  if (w->indent) cima__put(w, ": ", 2);
  else cima__putc(w, ':');
  w->after_key = true;
}

void cima_json_write_key(cima_json_writer *w, const char *key) { cima_json_write_key_n(w, key, strlen(key)); }

void cima_json_begin_object(cima_json_writer *w) {
  cima__pre(w);
  cima__putc(w, '{');
  w->depth++;
  w->comma = false;
}

void cima_json_end_object(cima_json_writer *w) {
  w->depth--;
  if (w->indent && w->comma) cima__newline(w);
  cima__putc(w, '}');
  w->comma = true;
}

void cima_json_begin_array(cima_json_writer *w) {
  cima__pre(w);
  cima__putc(w, '[');
  w->depth++;
  w->comma = false;
}

void cima_json_end_array(cima_json_writer *w) {
  w->depth--;
  if (w->indent && w->comma) cima__newline(w);
  cima__putc(w, ']');
  w->comma = true;
}

char *cima_json_writer_finish(cima_json_writer *w, size_t *len) {
  cima__reserve(w, 1);
  w->buf[w->len] = '\0';
  char *out = w->buf;
  // arenas and sized allocators get the exact size back, the heap does not need it.
  if (w->alloc && w->alloc != cima__heap()) out = cilo_realloc(w->alloc, out, w->cap, w->len + 1, 1);
  if (len) *len = w->len;
  *w = (cima_json_writer){.alloc = w->alloc, .indent = w->indent};
  return out;
}

void cima_json_writer_dispose(cima_json_writer *w) {
  if (w->buf) cilo_free(w->alloc ? w->alloc : cima__heap(), w->buf, w->cap);
  *w = (cima_json_writer){.alloc = w->alloc, .indent = w->indent};
}

static void write_key_member(cima_json_writer *w, const cima_json_member *m) {
  size_t len = cima_json_key_len(m);
  if ((m->flags & CIMA_JSON__CLEAN) && !w->indent) {
    cima__pre(w);
    char *d = cima__reserve(w, len + 3);
    d[0] = '"';
    memcpy(d + 1, m->key, len);
    d[len + 1] = '"';
    d[len + 2] = ':';
    w->len += len + 3;
    w->after_key = true;
    return;
  }
  cima_json_write_key_n(w, m->key, len);
}

void cima_json_write(cima_json_writer *w, const cima_json *v) {
  if (!v) {
    cima_json_write_null(w);
    return;
  }
  switch (v->type) {
  case CIMA_JSON_NULL: cima_json_write_null(w); return;
  case CIMA_JSON_BOOL: cima_json_write_bool(w, v->b); return;
  case CIMA_JSON_INT: cima_json_write_int(w, v->i); return;
  case CIMA_JSON_UINT: cima_json_write_uint(w, v->u); return;
  case CIMA_JSON_DOUBLE: cima_json_write_double(w, v->d); return;
  case CIMA_JSON_STRING:
    cima__pre(w);
    if (v->flags & CIMA_JSON__CLEAN) {
      char *d = cima__reserve(w, v->len + 2);
      d[0] = '"';
      memcpy(d + 1, v->str, v->len);
      d[v->len + 1] = '"';
      w->len += v->len + 2;
    } else {
      cima__write_quoted(w, v->str, v->len);
    }
    w->comma = true;
    return;
  case CIMA_JSON_ARRAY:
    cima_json_begin_array(w);
    for (uint32_t i = 0; i < v->len; i++) cima_json_write(w, &v->items[i]);
    cima_json_end_array(w);
    return;
  case CIMA_JSON_OBJECT:
    cima_json_begin_object(w);
    for (uint32_t i = 0; i < v->len; i++) {
      write_key_member(w, &v->members[i]);
      cima_json_write(w, &v->members[i].value);
    }
    cima_json_end_object(w);
    return;
  case CIMA_JSON_RAW: cima_json_write_raw(w, v->str, v->len); return;
  case CIMA_JSON_TYPED: cima_json_write_typed(w, v->typed->schema, v->typed->value); return;
  }
}

char *cima_json_text(cilo_allocator *a, const cima_json *v, size_t *len) {
  cima_json_writer w = {.alloc = a};
  cima_json_write(&w, v);
  return cima_json_writer_finish(&w, len);
}

char *cima_json_pretty(cilo_allocator *a, const cima_json *v, size_t *len) {
  cima_json_writer w = {.alloc = a, .indent = 2};
  cima_json_write(&w, v);
  return cima_json_writer_finish(&w, len);
}

/* ========================================================================================================== */
/* validation without a dom, for skipping values.                                                             */
/* ========================================================================================================== */

const char *cima__scan_string_rest(const char *start, const char *p, const char *end, cima__str *out,
                                   const char **err) {
  bool escaped = false;
  for (;;) {
    while (end - p >= 8) {
      uint64_t m = cima__special(cima__load64(p)) & CIMA__HIGHS;
      if (m) {
        p += __builtin_ctzll(m) >> 3;
        goto special;
      }
      p += 8;
    }
    while (p < end && (unsigned char)*p >= 0x20 && *p != '"' && *p != '\\' && (unsigned char)*p < 0x80) p++;
    if (p >= end) {
      *err = "unterminated string";
      return nullptr;
    }
  special:;
    unsigned char c = (unsigned char)*p;
    if (c == '"') {
      *out = (cima__str){start, (size_t)(p - start), escaped};
      return p + 1;
    }
    if (c == '\\') {
      escaped = true;
      if (p + 1 >= end) {
        *err = "unterminated string";
        return nullptr;
      }
      switch (p[1]) {
      case '"': case '\\': case '/': case 'b': case 'f': case 'n': case 'r': case 't':
        p += 2;
        continue;
      case 'u': {
        if (end - p < 6) break;
        int32_t u = cima__hex4(p + 2);
        if (u < 0) break;
        p += 6;
        if (u >= 0xdc00 && u <= 0xdfff) {
          *err = "lone low surrogate";
          return nullptr;
        }
        if (u >= 0xd800 && u <= 0xdbff) {
          int32_t lo = end - p >= 6 && p[0] == '\\' && p[1] == 'u' ? cima__hex4(p + 2) : -1;
          if (lo < 0xdc00 || lo > 0xdfff) {
            *err = "lone high surrogate";
            return nullptr;
          }
          p += 6;
        }
        continue;
      }
      default:
        break;
      }
      *err = "invalid escape";
      return nullptr;
    }
    if (c < 0x20) {
      *err = "control character in string";
      return nullptr;
    }
    size_t n = cima__utf8_seq((const unsigned char *)p, (const unsigned char *)end);
    if (!n) {
      *err = "invalid utf-8";
      return nullptr;
    }
    p += n;
  }
}

static const char *literal(const char *p, const char *end, const char *word, size_t n) {
  return (size_t)(end - p) >= n && memcmp(p, word, n) == 0 ? p + n : nullptr;
}

const char *cima__skip(const char *p, const char *end, uint32_t depth, cima__lex_error *err) {
  p = cima__ws(p, end);
  if (p >= end) return err->at = p, err->msg = "unexpected end of input", nullptr;
  switch (*p) {
  case '"': {
    cima__str s;
    const char *q = cima__scan_string_inline(p, end, &s, &err->msg);
    if (!q) err->at = p;
    return q;
  }
  case '{':
  case '[': {
    char close = *p == '{' ? '}' : ']';
    bool object = *p == '{';
    if (!depth) return err->at = p, err->msg = "nested too deep", nullptr;
    p = cima__ws(p + 1, end);
    if (p < end && *p == close) return p + 1;
    for (;;) {
      if (object) {
        if (p >= end || *p != '"') return err->at = p, err->msg = "expected a key", nullptr;
        cima__str s;
        const char *q = cima__scan_string_inline(p, end, &s, &err->msg);
        if (!q) return err->at = p, nullptr;
        p = cima__ws(q, end);
        if (p >= end || *p != ':') return err->at = p, err->msg = "expected ':'", nullptr;
        p++;
      }
      p = cima__skip(p, end, depth - 1, err);
      if (!p) return nullptr;
      p = cima__ws(p, end);
      if (p < end && *p == ',') {
        p = cima__ws(p + 1, end);
        continue;
      }
      if (p < end && *p == close) return p + 1;
      return err->at = p, err->msg = object ? "expected ',' or '}'" : "expected ',' or ']'", nullptr;
    }
  }
  case 't': {
    const char *q = literal(p, end, "true", 4);
    if (!q) err->at = p, err->msg = "invalid literal";
    return q;
  }
  case 'f': {
    const char *q = literal(p, end, "false", 5);
    if (!q) err->at = p, err->msg = "invalid literal";
    return q;
  }
  case 'n': {
    const char *q = literal(p, end, "null", 4);
    if (!q) err->at = p, err->msg = "invalid literal";
    return q;
  }
  default: {
    cima__num n;
    const char *q = cima__scan_number(p, end, &n);
    if (!q) err->at = p, err->msg = *p == '-' || (*p >= '0' && *p <= '9') ? "invalid number" : "unexpected character";
    return q;
  }
  }
}

bool cima_json_valid(const char *text, size_t len) {
  cima__lex_error err;
  const char *end = text + len;
  const char *p = cima__skip(text, end, 512, &err);
  return p && cima__ws(p, end) == end;
}

/* ========================================================================================================== */
/* dom parser. the text is copied into the arena once: strings and keys point into the copy, ended by a nul   */
/* written over their closing quote, and escaped ones are decoded in place. a loop over a stack of open        */
/* containers gathers their children in a scratch arena, and each container takes an exact copy when it ends. */
/* ========================================================================================================== */

constexpr uint32_t DOM_MAX_DEPTH = 512;

typedef struct dom {
  cilo_arena *a;
  cilo_arena *tmp;
  const char *msg;
} dom;

// an open container: where its children start on their stack.
typedef struct dom_frame {
  size_t base;
  bool object;
} dom_frame;

typedef struct dom_stack {
  void *items;
  size_t cap;
} dom_stack;

[[gnu::noinline]] static dom_stack dom_grow(cilo_arena *tmp, dom_stack s, size_t size, size_t first) {
  size_t cap = s.cap ? s.cap * 2 : first;
  return (dom_stack){cilo_arena_realloc(tmp, s.items, s.cap * size, cap * size, 16), cap};
}

// the string whose opening quote is at p, in the copy. returns what follows it, or null with `msg` set.
[[gnu::always_inline]] static inline char *dom_string(dom *d, char *p, const char *end, const char **str,
                                                      uint32_t *len, uint8_t *flags) {
  cima__str s;
  const char *q = cima__scan_string_inline(p, end, &s, &d->msg);
  if (!q) return nullptr;
  if (s.len > UINT32_MAX) {
    d->msg = "string too long";
    return nullptr;
  }
  char *body = p + 1;
  size_t n = s.len;
  if (s.escaped) {
    n = cima__unescape(body, body, n);
    *flags = 0;
  } else {
    // no escapes in the source means no quote, backslash or control character, and the utf-8 is valid.
    *flags = CIMA_JSON__CLEAN;
  }
  body[n] = '\0';
  *str = body;
  *len = (uint32_t)n;
  return (char *)q;
}

// the copy ends with a nul, which is not whitespace, so skipping whitespace in it needs no bounds check.
static inline char *dom_ws(char *p) {
  while ((unsigned char)*p <= ' ' && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
  return p;
}

// parses one value from p into `root`. returns where it stopped, the error position when `d->msg` is set. the
// stacks live in locals, so stores of children never make the compiler reload them.
static char *dom_parse(dom *d, char *p, const char *end, cima_json *root) {
  dom_frame frames[DOM_MAX_DEPTH];
  uint32_t depth = 0;
  bool object = false;
  dom_stack item_stack = {}, member_stack = {};
  cima_json *items = nullptr;
  cima_json_member *members = nullptr;
  size_t items_len = 0, members_len = 0;
  cima_json v;
  char *q;
value:
  p = dom_ws(p);
  if (p >= end) return d->msg = "unexpected end of input", p;
  switch (*p) {
  case '{':
    if (depth == DOM_MAX_DEPTH) return d->msg = "nested too deep", p;
    p = dom_ws(p + 1);
    if (p < end && *p == '}') {
      p++;
      v = (cima_json){.type = CIMA_JSON_OBJECT};
      goto done;
    }
    frames[depth++] = (dom_frame){members_len, true};
    object = true;
    goto key;
  case '[':
    if (depth == DOM_MAX_DEPTH) return d->msg = "nested too deep", p;
    p = dom_ws(p + 1);
    if (p < end && *p == ']') {
      p++;
      v = (cima_json){.type = CIMA_JSON_ARRAY};
      goto done;
    }
    frames[depth++] = (dom_frame){items_len, false};
    object = false;
    goto value;
  case '"':
    v = (cima_json){.type = CIMA_JSON_STRING};
    if (!(q = dom_string(d, p, end, &v.str, &v.len, &v.flags))) return p;
    p = q;
    goto done;
  case 't':
    if (!literal(p, end, "true", 4)) return d->msg = "invalid literal", p;
    v = (cima_json){.type = CIMA_JSON_BOOL, .b = true};
    p += 4;
    goto done;
  case 'f':
    if (!literal(p, end, "false", 5)) return d->msg = "invalid literal", p;
    v = (cima_json){.type = CIMA_JSON_BOOL, .b = false};
    p += 5;
    goto done;
  case 'n':
    if (!literal(p, end, "null", 4)) return d->msg = "invalid literal", p;
    v = (cima_json){};
    p += 4;
    goto done;
  default: {
    // small integers, most numbers in practice, without the general scan: up to 18 digits never overflow.
    if (*p >= '1' && *p <= '9') {
      uint64_t u = (uint64_t)(*p - '0');
      q = p + 1;
      const char *stop = end - q > 17 ? q + 17 : end;
      while (q < stop && *q >= '0' && *q <= '9') u = u * 10 + (uint64_t)(*q++ - '0');
      if (q == end || (*q != '.' && *q != 'e' && *q != 'E' && (*q < '0' || *q > '9'))) {
        v = (cima_json){.type = CIMA_JSON_INT, .i = (int64_t)u};
        p = q;
        goto done;
      }
    }
    cima__num n;
    if (!(q = (char *)cima__scan_number(p, end, &n))) {
      bool numeric = *p == '-' || (*p >= '0' && *p <= '9');
      return d->msg = numeric ? "invalid number" : "unexpected character", p;
    }
    uint64_t mag;
    if (n.plain && cima__num_magnitude(&n, &mag) == CIMA__INT_OK) {
      if (!n.neg) {
        v = mag <= INT64_MAX ? (cima_json){.type = CIMA_JSON_INT, .i = (int64_t)mag}
                             : (cima_json){.type = CIMA_JSON_UINT, .u = mag};
        p = q;
        goto done;
      }
      if (mag <= (uint64_t)INT64_MAX + 1) {
        v = (cima_json){.type = CIMA_JSON_INT, .i = (int64_t)(0 - mag)};
        p = q;
        goto done;
      }
    }
    double x = cima__num_double(&n, p, (size_t)(q - p));
    if (!isfinite(x)) return d->msg = "number out of range", p;
    v = (cima_json){.type = CIMA_JSON_DOUBLE, .d = x};
    p = q;
    goto done;
  }
  }
done:
  if (!depth) {
    *root = v;
    return p;
  }
  if (object) {
    members[members_len - 1].value = v;
  } else {
    if (UNLIKELY(items_len == item_stack.cap)) {
      item_stack = dom_grow(d->tmp, item_stack, sizeof *items, 256);
      items = item_stack.items;
    }
    items[items_len++] = v;
  }
  p = dom_ws(p);
  if (p < end && *p == ',') {
    p++;
    if (!object) goto value;
    p = dom_ws(p);
    goto key;
  }
  if (p >= end || *p != (object ? '}' : ']'))
    return d->msg = object ? "expected ',' or '}'" : "expected ',' or ']'", p;
  p++;
  size_t base = frames[--depth].base;
  if (object) {
    size_t n = members_len - base;
    if (n > UINT32_MAX) return d->msg = "object too long", p;
    cima_json_member *m = cilo_arena_alloc(d->a, n * sizeof *m, alignof(cima_json_member));
    memcpy(m, members + base, n * sizeof *m);
    members_len = base;
    v = (cima_json){.type = CIMA_JSON_OBJECT, .len = (uint32_t)n, .members = m};
  } else {
    size_t n = items_len - base;
    if (n > UINT32_MAX) return d->msg = "array too long", p;
    cima_json *copy = cilo_arena_alloc(d->a, n * sizeof *copy, alignof(cima_json));
    memcpy(copy, items + base, n * sizeof *copy);
    items_len = base;
    v = (cima_json){.type = CIMA_JSON_ARRAY, .len = (uint32_t)n, .items = copy};
  }
  object = depth && frames[depth - 1].object;
  goto done;
key:
  if (p >= end || *p != '"') return d->msg = "expected a key", p;
  if (UNLIKELY(members_len == member_stack.cap)) {
    member_stack = dom_grow(d->tmp, member_stack, sizeof *members, 128);
    members = member_stack.items;
  }
  {
    cima_json_member *m = &members[members_len];
    *m = (cima_json_member){};
    if (!(q = dom_string(d, p, end, &m->key, &m->key_len, &m->flags))) return p;
    members_len++;
    p = q;
  }
  p = dom_ws(p);
  if (p >= end || *p != ':') return d->msg = "expected ':'", p;
  p++;
  goto value;
}

static cima_json *node_in(cilo_arena *a, cima_json v) {
  cima_json *node = cilo_arena_alloc(a, sizeof v, alignof(cima_json));
  *node = v;
  return node;
}

cima_json *cima_json_parse(cilo_arena *a, const char *text, size_t len, cima_json_error *err) {
  cilo_mark start = cilo_mark_of(a);
  char *copy = cilo_arena_alloc(a, len + 1, 1);
  if (len) memcpy(copy, text, len);
  copy[len] = '\0';
  const char *end = copy + len;
  dom d = {.a = a};
  cima_json v;
  char *p = copy;
  CILO_SCRATCH(tmp, a) {
    d.tmp = tmp;
    p = dom_parse(&d, copy, end, &v);
    if (!d.msg) {
      p = (char *)cima__ws(p, end);
      if (p != end) d.msg = "trailing characters";
    }
  }
  if (d.msg) {
    if (err) *err = (cima_json_error){(size_t)(p - copy), d.msg};
    // nothing points into what the parse allocated, it all goes back.
    cilo_rewind(start);
    return nullptr;
  }
  return node_in(a, v);
}

/* ========================================================================================================== */
/* builders.                                                                                                  */
/* ========================================================================================================== */

cima_json cima_json__string(const char *s) {
  if (!s) return (cima_json){};
  return (cima_json){.type = CIMA_JSON_STRING, .flags = CIMA_JSON_TEMP, .len = (uint32_t)strlen(s), .str = s};
}

cima_json *cima_json__obj(cima_json *node, cima_json_member *members) {
  uint32_t n = 0;
  while (members[n].key) n++;
  *node = (cima_json){.type = CIMA_JSON_OBJECT, .flags = CIMA_JSON_TEMP, .len = n, .members = members};
  return node;
}

cima_json *cima_json__arr(cima_json *node, cima_json *items) {
  uint32_t n = 0;
  while (items[n].flags != 0xff) n++;
  *node = (cima_json){.type = CIMA_JSON_ARRAY, .flags = CIMA_JSON_TEMP, .len = n, .items = items};
  return node;
}

cima_json *cima_json__raw(cima_json *node, const char *json) {
  *node = (cima_json){.type = CIMA_JSON_RAW, .flags = CIMA_JSON_TEMP, .len = (uint32_t)strlen(json), .str = json};
  return node;
}

/* ========================================================================================================== */
/* reading.                                                                                                   */
/* ========================================================================================================== */

// a repeated key reads as its last value, like javascript and serde_json.
static ptrdiff_t find_member(const cima_json *o, const char *key, size_t len) {
  for (uint32_t i = o->len; i-- > 0;) {
    const cima_json_member *m = &o->members[i];
    if (cima_json_key_len(m) == len && memcmp(m->key, key, len) == 0) return i;
  }
  return -1;
}

cima_json *cima_json_get_n(const cima_json *o, const char *key, size_t len) {
  if (!o || o->type != CIMA_JSON_OBJECT) return nullptr;
  ptrdiff_t i = find_member(o, key, len);
  return i < 0 ? nullptr : &o->members[i].value;
}

cima_json *cima_json_get(const cima_json *o, const char *key) { return cima_json_get_n(o, key, strlen(key)); }

cima_json *cima_json_index(const cima_json *array, size_t index) {
  return array && array->type == CIMA_JSON_ARRAY && index < array->len ? &array->items[index] : nullptr;
}

cima_json *cima_json_at(const cima_json *root, const char *pointer) {
  cima_json *v = (cima_json *)root;
  const char *p = pointer;
  if (*p && *p != '/') return nullptr;
  while (v && *p) {
    p++;
    const char *end = p;
    while (*end && *end != '/') end++;
    size_t n = (size_t)(end - p);
    if (v->type == CIMA_JSON_ARRAY) {
      if (n == 0 || (n > 1 && *p == '0')) return nullptr;
      size_t index = 0;
      for (const char *c = p; c < end; c++) {
        if (*c < '0' || *c > '9' || ckd_mul(&index, index, 10) || ckd_add(&index, index, (size_t)(*c - '0')))
          return nullptr;
      }
      v = cima_json_index(v, index);
    } else if (memchr(p, '~', n)) {
      char key[256];
      if (n > sizeof key) return nullptr;
      size_t k = 0;
      for (const char *c = p; c < end; c++) {
        if (*c == '~' && c + 1 < end && (c[1] == '0' || c[1] == '1')) key[k++] = *++c == '0' ? '~' : '/';
        else key[k++] = *c;
      }
      v = cima_json_get_n(v, key, k);
    } else {
      v = cima_json_get_n(v, p, n);
    }
    p = end;
  }
  return v;
}

bool cima_json_as_bool(const cima_json *v, bool fallback) {
  return v && v->type == CIMA_JSON_BOOL ? v->b : fallback;
}

int64_t cima_json_as_int(const cima_json *v, int64_t fallback) {
  if (!v) return fallback;
  if (v->type == CIMA_JSON_INT) return v->i;
  if (v->type == CIMA_JSON_DOUBLE && v->d >= -0x1p63 && v->d < 0x1p63 && v->d == (double)(int64_t)v->d)
    return (int64_t)v->d;
  return fallback;
}

uint64_t cima_json_as_uint(const cima_json *v, uint64_t fallback) {
  if (!v) return fallback;
  if (v->type == CIMA_JSON_INT && v->i >= 0) return (uint64_t)v->i;
  if (v->type == CIMA_JSON_UINT) return v->u;
  if (v->type == CIMA_JSON_DOUBLE && v->d >= 0 && v->d < 0x1p64 && v->d == (double)(uint64_t)v->d)
    return (uint64_t)v->d;
  return fallback;
}

double cima_json_as_double(const cima_json *v, double fallback) {
  if (!v) return fallback;
  switch (v->type) {
  case CIMA_JSON_INT: return (double)v->i;
  case CIMA_JSON_UINT: return (double)v->u;
  case CIMA_JSON_DOUBLE: return v->d;
  default: return fallback;
  }
}

const char *cima_json_as_string(const cima_json *v, const char *fallback) {
  return v && v->type == CIMA_JSON_STRING ? v->str : fallback;
}

/* ========================================================================================================== */
/* keeping and changing.                                                                                      */
/* ========================================================================================================== */

static bool clean_text(const char *s, size_t len) { return clean_run(s, s + len) == s + len; }

static const char *copy_text(cilo_arena *a, const char *s, size_t len) {
  char *d = cilo_arena_alloc(a, len + 1, 1);
  memcpy(d, s, len);
  d[len] = '\0';
  return d;
}

static void encode_typed(cilo_arena *a, cima_json *v) {
  cima_json_writer w = {.alloc = CILO_OF(a)};
  cima_json_write_typed(&w, v->typed->schema, v->typed->value);
  size_t len;
  const char *text = cima_json_writer_finish(&w, &len);
  *v = (cima_json){.type = CIMA_JSON_RAW, .len = (uint32_t)len, .str = text};
}

// copies into `a` what `v` does not own yet, or everything when `deep`.
static void keep(cilo_arena *a, cima_json *v, bool deep) {
  if (!deep && !(v->flags & CIMA_JSON_TEMP)) return;
  switch (v->type) {
  case CIMA_JSON_STRING:
  case CIMA_JSON_RAW:
    if (v->type == CIMA_JSON_STRING && !(v->flags & CIMA_JSON__CLEAN) && clean_text(v->str, v->len))
      v->flags |= CIMA_JSON__CLEAN;
    v->str = copy_text(a, v->str, v->len);
    break;
  case CIMA_JSON_ARRAY: {
    cima_json *items = cilo_arena_alloc(a, (size_t)v->len * sizeof(cima_json) + 1, alignof(cima_json));
    if (v->len) memcpy(items, v->items, (size_t)v->len * sizeof(cima_json));
    for (uint32_t i = 0; i < v->len; i++) keep(a, &items[i], deep);
    v->items = items;
    v->flags &= (uint8_t)~CIMA_JSON__GROW;
    break;
  }
  case CIMA_JSON_OBJECT: {
    cima_json_member *members =
        cilo_arena_alloc(a, (size_t)v->len * sizeof(cima_json_member) + 1, alignof(cima_json_member));
    if (v->len) memcpy(members, v->members, (size_t)v->len * sizeof(cima_json_member));
    for (uint32_t i = 0; i < v->len; i++) {
      cima_json_member *m = &members[i];
      if (deep || (v->flags & CIMA_JSON_TEMP)) {
        size_t len = cima_json_key_len(m);
        m->key = copy_text(a, m->key, len);
        m->key_len = (uint32_t)len;
        m->flags = clean_text(m->key, len) ? CIMA_JSON__CLEAN : 0;
      }
      keep(a, &m->value, deep);
    }
    v->members = members;
    v->flags &= (uint8_t)~CIMA_JSON__GROW;
    break;
  }
  case CIMA_JSON_TYPED: encode_typed(a, v); break;
  default: break;
  }
  v->flags &= (uint8_t)~CIMA_JSON_TEMP;
}

cima_json *cima_json_clone(cilo_arena *a, const cima_json *v) {
  cima_json *copy = node_in(a, v ? *v : (cima_json){});
  keep(a, copy, true);
  return copy;
}

static size_t capacity(const cima_json *v) {
  if (!(v->flags & CIMA_JSON__GROW)) return v->len;
  return v->len <= 4 ? 4 : (size_t)1 << (64 - __builtin_clzll((uint64_t)v->len - 1));
}

// room for one more child at the end.
static void grow(cilo_arena *a, cima_json *v, size_t size, size_t align) {
  if (v->flags & CIMA_JSON_TEMP) keep(a, v, false);
  size_t cap = capacity(v);
  if (v->len < cap) return;
  size_t want = cap < 4 ? 4 : cap * 2;
  void *old = v->type == CIMA_JSON_ARRAY ? (void *)v->items : (void *)v->members;
  void *fresh = (v->flags & CIMA_JSON__GROW) ? cilo_arena_realloc(a, old, cap * size, want * size, align)
                                             : cilo_arena_alloc(a, want * size, align);
  if (!(v->flags & CIMA_JSON__GROW) && v->len) memcpy(fresh, old, v->len * size);
  if (v->type == CIMA_JSON_ARRAY) v->items = fresh;
  else v->members = fresh;
  v->flags |= CIMA_JSON__GROW;
}

cima_json *cima_json_set(cilo_arena *a, cima_json *o, const char *key, const cima_json *value) {
  if (!o || o->type != CIMA_JSON_OBJECT) return nullptr;
  if (o->flags & CIMA_JSON_TEMP) keep(a, o, false);
  size_t len = strlen(key);
  ptrdiff_t i = find_member(o, key, len);
  if (i >= 0) {
    cima_json *slot = &o->members[i].value;
    *slot = value ? *value : (cima_json){};
    keep(a, slot, false);
    return slot;
  }
  return cima_json_insert(a, o, o->len, key, value);
}

cima_json *cima_json_insert(cilo_arena *a, cima_json *o, size_t index, const char *key, const cima_json *value) {
  if (!o || o->type != CIMA_JSON_OBJECT) return nullptr;
  size_t len = strlen(key);
  ptrdiff_t old = find_member(o, key, len);
  if (old >= 0) cima_json_remove(o, key);
  grow(a, o, sizeof(cima_json_member), alignof(cima_json_member));
  if (index > o->len) index = o->len;
  memmove(&o->members[index + 1], &o->members[index], (o->len - index) * sizeof(cima_json_member));
  o->len++;
  cima_json_member *m = &o->members[index];
  *m = (cima_json_member){.key = copy_text(a, key, len), .key_len = (uint32_t)len};
  m->flags = clean_text(key, len) ? CIMA_JSON__CLEAN : 0;
  m->value = value ? *value : (cima_json){};
  keep(a, &m->value, false);
  return &m->value;
}

cima_json *cima_json_obj_push(cilo_arena *a, cima_json *o, const char *key, const cima_json *value) {
  if (!o || o->type != CIMA_JSON_OBJECT) return nullptr;
  grow(a, o, sizeof(cima_json_member), alignof(cima_json_member));
  size_t len = strlen(key);
  cima_json_member *m = &o->members[o->len++];
  *m = (cima_json_member){.key = key, .key_len = (uint32_t)len, .value = value ? *value : (cima_json){}};
  m->flags = clean_text(key, len) ? CIMA_JSON__CLEAN : 0;
  keep(a, &m->value, false);
  return &m->value;
}

bool cima_json_remove(cima_json *o, const char *key) {
  if (!o || o->type != CIMA_JSON_OBJECT) return false;
  ptrdiff_t i = find_member(o, key, strlen(key));
  if (i < 0) return false;
  memmove(&o->members[i], &o->members[i + 1], (o->len - (size_t)i - 1) * sizeof(cima_json_member));
  o->len--;
  return true;
}

cima_json *cima_json_append(cilo_arena *a, cima_json *array, const cima_json *value) {
  if (!array || array->type != CIMA_JSON_ARRAY) return nullptr;
  grow(a, array, sizeof(cima_json), alignof(cima_json));
  cima_json *slot = &array->items[array->len++];
  *slot = value ? *value : (cima_json){};
  keep(a, slot, false);
  return slot;
}

bool cima_json_remove_at(cima_json *array, size_t index) {
  if (!array || array->type != CIMA_JSON_ARRAY || index >= array->len) return false;
  memmove(&array->items[index], &array->items[index + 1], (array->len - index - 1) * sizeof(cima_json));
  array->len--;
  return true;
}

/* ========================================================================================================== */
/* comparing.                                                                                                 */
/* ========================================================================================================== */

// a key index for big objects, linear scans do better under this size.
constexpr uint32_t INDEX_FROM = 12;

typedef struct key_index {
  const cima_json *object;
  uint32_t *slots;
  uint32_t mask;
} key_index;

static void index_build(cilo_arena *tmp, key_index *ix, const cima_json *o) {
  *ix = (key_index){.object = o};
  if (!o || o->len < INDEX_FROM) return;
  uint32_t size = 32;
  while (size < o->len * 2) size <<= 1;
  ix->slots = cilo_arena_alloc_zeroed(tmp, size * sizeof(uint32_t), alignof(uint32_t));
  ix->mask = size - 1;
  for (uint32_t i = 0; i < o->len; i++) {
    const cima_json_member *m = &o->members[i];
    size_t len = cima_json_key_len(m);
    uint32_t h = (uint32_t)cima__hash(m->key, len) & ix->mask;
    // a repeated key takes the slot of the earlier one, the last value wins.
    while (ix->slots[h]) {
      const cima_json_member *e = &o->members[ix->slots[h] - 1];
      if (cima_json_key_len(e) == len && memcmp(e->key, m->key, len) == 0) break;
      h = (h + 1) & ix->mask;
    }
    ix->slots[h] = i + 1;
  }
}

static ptrdiff_t index_find(const key_index *ix, const char *key, size_t len) {
  const cima_json *o = ix->object;
  if (!o || o->type != CIMA_JSON_OBJECT) return -1;
  if (!ix->slots) return find_member(o, key, len);
  for (uint32_t h = (uint32_t)cima__hash(key, len) & ix->mask; ix->slots[h]; h = (h + 1) & ix->mask) {
    const cima_json_member *m = &o->members[ix->slots[h] - 1];
    if (cima_json_key_len(m) == len && memcmp(m->key, key, len) == 0) return ix->slots[h] - 1;
  }
  return -1;
}

static bool eq(cilo_arena *tmp, const cima_json *a, const cima_json *b);

// raw and typed nodes compare by what they hold.
static const cima_json *settle(cilo_arena *tmp, const cima_json *v) {
  if (v->type == CIMA_JSON_TYPED) {
    cima_json copy = *v;
    encode_typed(tmp, &copy);
    v = node_in(tmp, copy);
  }
  if (v->type == CIMA_JSON_RAW) {
    const cima_json *parsed = cima_json_parse(tmp, v->str, v->len, nullptr);
    return parsed ? parsed : CILO_NEW(tmp, cima_json);
  }
  return v;
}

static bool num_eq(const cima_json *a, const cima_json *b) {
  if (a->type == b->type) {
    switch (a->type) {
    case CIMA_JSON_INT: return a->i == b->i;
    case CIMA_JSON_UINT: return a->u == b->u;
    default: return a->d == b->d;
    }
  }
  if (a->type == CIMA_JSON_DOUBLE || b->type == CIMA_JSON_DOUBLE) {
    const cima_json *d = a->type == CIMA_JSON_DOUBLE ? a : b, *i = a->type == CIMA_JSON_DOUBLE ? b : a;
    if (i->type == CIMA_JSON_INT)
      return d->d >= -0x1p63 && d->d < 0x1p63 && (int64_t)d->d == i->i && d->d == (double)(int64_t)d->d;
    return d->d >= 0 && d->d < 0x1p64 && (uint64_t)d->d == i->u && d->d == (double)(uint64_t)d->d;
  }
  // an int and a uint never hold the same value.
  return false;
}

static bool eq(cilo_arena *tmp, const cima_json *a, const cima_json *b) {
  if (!a || !b) return a == b;
  if (a->type >= CIMA_JSON_RAW) a = settle(tmp, a);
  if (b->type >= CIMA_JSON_RAW) b = settle(tmp, b);
  if (cima_json_is_number(a) && cima_json_is_number(b)) return num_eq(a, b);
  if (a->type != b->type) return false;
  switch (a->type) {
  case CIMA_JSON_NULL: return true;
  case CIMA_JSON_BOOL: return a->b == b->b;
  case CIMA_JSON_STRING: return a->len == b->len && memcmp(a->str, b->str, a->len) == 0;
  case CIMA_JSON_ARRAY:
    if (a->len != b->len) return false;
    for (uint32_t i = 0; i < a->len; i++)
      if (!eq(tmp, &a->items[i], &b->items[i])) return false;
    return true;
  case CIMA_JSON_OBJECT: {
    key_index in_a, in_b;
    index_build(tmp, &in_a, a);
    index_build(tmp, &in_b, b);
    // keys repeated in one object count once, with their last value.
    size_t live_a = 0, live_b = 0;
    for (uint32_t i = 0; i < a->len; i++) {
      const cima_json_member *m = &a->members[i];
      size_t len = cima_json_key_len(m);
      if (index_find(&in_a, m->key, len) != i) continue;
      live_a++;
      ptrdiff_t j = index_find(&in_b, m->key, len);
      if (j < 0 || !eq(tmp, &m->value, &b->members[j].value)) return false;
    }
    for (uint32_t i = 0; i < b->len; i++)
      live_b += index_find(&in_b, b->members[i].key, cima_json_key_len(&b->members[i])) == i;
    return live_a == live_b;
  }
  default: return false;
  }
}

bool cima_json_eq(const cima_json *a, const cima_json *b) {
  bool same;
  CILO_SCRATCH(tmp) same = eq(tmp, a, b);
  return same;
}

cima_json_changes cima_json_diff(cilo_arena *a, const cima_json *before, const cima_json *after) {
  cima_json_members old = cima_json_object(before), now = cima_json_object(after);
  cima_json_changes out = {};
  if (!old.len && !now.len) return out;
  out.items = cilo_arena_alloc(a, (old.len + now.len) * sizeof(cima_json_change), alignof(cima_json_change));
  CILO_SCRATCH(tmp, a) {
    key_index in_old, in_now;
    index_build(tmp, &in_old, old.len ? before : nullptr);
    index_build(tmp, &in_now, now.len ? after : nullptr);
    ptrdiff_t last = -1;
    for (size_t i = 0; i < now.len; i++) {
      const cima_json_member *m = &now.items[i];
      size_t len = cima_json_key_len(m);
      ptrdiff_t j = index_find(&in_old, m->key, len);
      if (j < 0) {
        out.items[out.len++] = (cima_json_change){CIMA_JSON_ADDED, m->key, len, nullptr, &m->value};
        continue;
      }
      out.reordered |= j < last;
      last = j;
      if (!eq(tmp, &old.items[j].value, &m->value))
        out.items[out.len++] = (cima_json_change){CIMA_JSON_CHANGED, m->key, len, &old.items[j].value, &m->value};
    }
    for (size_t i = 0; i < old.len; i++) {
      const cima_json_member *m = &old.items[i];
      size_t len = cima_json_key_len(m);
      if (index_find(&in_now, m->key, len) < 0)
        out.items[out.len++] = (cima_json_change){CIMA_JSON_REMOVED, m->key, len, &m->value, nullptr};
    }
  }
  return out;
}

cima_json *cima_json_patch(cilo_arena *a, const cima_json *before, const cima_json *after) {
  if (!before || !after || before->type != CIMA_JSON_OBJECT || after->type != CIMA_JSON_OBJECT)
    return (cima_json *)after;
  cima_json *out = CILO_NEW(a, cima_json, .type = CIMA_JSON_OBJECT);
  size_t cap = (size_t)after->len + before->len;
  out->members = cilo_arena_alloc(a, cap * sizeof(cima_json_member) + 1, alignof(cima_json_member));
  if (after->len) memcpy(out->members, after->members, after->len * sizeof(cima_json_member));
  size_t n = after->len;
  CILO_SCRATCH(tmp, a) {
    key_index in_now;
    index_build(tmp, &in_now, after);
    for (uint32_t i = 0; i < before->len; i++) {
      const cima_json_member *m = &before->members[i];
      if (index_find(&in_now, m->key, cima_json_key_len(m)) < 0)
        out->members[n++] = (cima_json_member){.key = m->key, .key_len = m->key_len, .flags = m->flags};
    }
  }
  out->len = (uint32_t)n;
  // builder parts of `after` are copied, the rest is shared.
  out->flags = after->flags & CIMA_JSON_TEMP;
  keep(a, out, false);
  return out;
}
