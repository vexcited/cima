#include "internal.h"

#include <pthread.h>
#include <regex.h>
#include <stdatomic.h>
#include <stdlib.h>

uint64_t cima__hash(const void *p, size_t len) {
  const unsigned char *s = p;
  uint64_t h = 0x9e3779b97f4a7c15ull ^ len;
  while (len >= 8) {
    uint64_t v;
    memcpy(&v, s, 8);
    h = (h ^ v) * 0xbf58476d1ce4e5b9ull;
    h ^= h >> 31;
    s += 8;
    len -= 8;
  }
  uint64_t v = 0;
  memcpy(&v, s, len);
  h = (h ^ v) * 0x94d049bb133111ebull;
  return h ^ (h >> 29);
}

/* a table that only grows: lookups are lock free, inserts take a mutex. entries are never freed. */

typedef struct cache_entry {
  struct cache_entry *next;
  const void *k1, *k2;
  uint32_t tag;
  _Atomic(void *) data;
} cache_entry;

constexpr size_t CACHE_BUCKETS = 1024;
static _Atomic(cache_entry *) cache[CACHE_BUCKETS];
static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;

static size_t bucket_of(const void *k1, const void *k2, uint32_t tag) {
  uintptr_t parts[3] = {(uintptr_t)k1, (uintptr_t)k2, tag};
  return cima__hash(parts, sizeof parts) & (CACHE_BUCKETS - 1);
}

static cache_entry *find(size_t b, const void *k1, const void *k2, uint32_t tag) {
  for (cache_entry *e = atomic_load_explicit(&cache[b], memory_order_acquire); e; e = e->next)
    if (e->k1 == k1 && e->k2 == k2 && e->tag == tag) return e;
  return nullptr;
}

void *cima__cache_get(const void *k1, const void *k2, uint32_t tag) {
  cache_entry *e = find(bucket_of(k1, k2, tag), k1, k2, tag);
  return e ? atomic_load_explicit(&e->data, memory_order_acquire) : nullptr;
}

void *cima__cache_put(const void *k1, const void *k2, uint32_t tag, void *data, bool replace) {
  size_t b = bucket_of(k1, k2, tag);
  pthread_mutex_lock(&cache_lock);
  cache_entry *e = find(b, k1, k2, tag);
  void *won = data;
  if (e && replace) {
    // readers may still hold the old data, it is never freed.
    atomic_store_explicit(&e->data, data, memory_order_release);
  } else if (e) {
    won = atomic_load_explicit(&e->data, memory_order_relaxed);
  } else {
    e = malloc(sizeof *e);
    if (!e) cilo_oom(sizeof *e);
    *e = (cache_entry){.next = atomic_load_explicit(&cache[b], memory_order_relaxed), .k1 = k1, .k2 = k2, .tag = tag};
    atomic_init(&e->data, data);
    atomic_store_explicit(&cache[b], e, memory_order_release);
  }
  pthread_mutex_unlock(&cache_lock);
  return won;
}


/* objects. */

static size_t rename_into(char *out, const char *name, cima_rename style) {
  size_t n = 0;
  bool upper = style == CIMA_RENAME_PASCAL, seen = false;
  for (const char *p = name; *p; p++) {
    char c = *p;
    if (c == '_' && seen && style != CIMA_RENAME_NONE) {
      if (style == CIMA_RENAME_KEBAB) out[n++] = '-';
      else upper = true;
      continue;
    }
    if (upper && c >= 'a' && c <= 'z') c = (char)(c - 32);
    upper = false;
    seen |= c != '_';
    out[n++] = c;
  }
  return n;
}

static void *must_malloc(size_t size) {
  void *p = calloc(1, size);
  if (!p) cilo_oom(size);
  return p;
}

static const cima__object_info *build_info(const cima_schema *s) {
  size_t count = s->fields_count;
  cima__object_info *info = must_malloc(sizeof *info);
  *info = (cima__object_info){.fields = s->fields, .count = count, .rename = s->object.rename};
  info->names = must_malloc(sizeof(char *) * (count + 1));
  info->lens = must_malloc(sizeof(uint32_t) * (count + 1));
  info->quoted = must_malloc(sizeof(char *) * (count + 1));
  info->quoted_lens = must_malloc(sizeof(uint32_t) * (count + 1));
  for (size_t i = 0; i < count; i++) {
    const char *name = s->fields[i].name;
    char *json = must_malloc(strlen(name) * 2 + 1);
    size_t n = rename_into(json, name, s->object.rename);
    info->names[i] = json;
    info->lens[i] = (uint32_t)n;
    cima_json_writer w = {};
    cima__write_quoted(&w, json, n);
    cima__putc(&w, ':');
    char *quoted = must_malloc(w.len + 1);
    memcpy(quoted, w.buf, w.len);
    info->quoted[i] = quoted;
    info->quoted_lens[i] = (uint32_t)w.len;
    cima_json_writer_dispose(&w);
  }
  if (count > 8) {
    uint32_t size = 16;
    while (size < count * 2) size <<= 1;
    info->slots = must_malloc(sizeof(uint32_t) * size);
    info->mask = size - 1;
    for (size_t i = 0; i < count; i++) {
      uint32_t h = (uint32_t)cima__hash(info->names[i], info->lens[i]) & info->mask;
      while (info->slots[h]) h = (h + 1) & info->mask;
      info->slots[h] = (uint32_t)i + 1;
    }
  }
  return info;
}

static void free_info(const cima__object_info *info) {
  for (size_t i = 0; i < info->count; i++) {
    free((void *)info->names[i]);
    free((void *)info->quoted[i]);
  }
  free(info->names);
  free(info->lens);
  free(info->quoted);
  free(info->quoted_lens);
  free(info->slots);
  free((void *)info);
}

const cima__object_info *cima__object_info_of(const cima_schema *s) {
  const cima__object_info *info = cima__cache_get(s, s->fields, CIMA__TAG_OBJECT);
  // a schema built at runtime may come back at the same address with other fields.
  if (LIKELY(info && info->count == s->fields_count && info->rename == s->object.rename)) return info;
  const cima__object_info *fresh = build_info(s);
  const cima__object_info *won = cima__cache_put(s, s->fields, CIMA__TAG_OBJECT, (void *)fresh, info != nullptr);
  if (won != fresh) free_info(fresh);
  return won;
}

ptrdiff_t cima__find_field(const cima__object_info *info, const char *key, size_t len, size_t hint) {
  if (hint < info->count && info->lens[hint] == len && memcmp(info->names[hint], key, len) == 0)
    return (ptrdiff_t)hint;
  if (info->slots) {
    for (uint32_t h = (uint32_t)cima__hash(key, len) & info->mask; info->slots[h]; h = (h + 1) & info->mask) {
      uint32_t i = info->slots[h] - 1;
      if (info->lens[i] == len && memcmp(info->names[i], key, len) == 0) return i;
    }
    return -1;
  }
  for (size_t i = 0; i < info->count; i++)
    if (info->lens[i] == len && memcmp(info->names[i], key, len) == 0) return (ptrdiff_t)i;
  return -1;
}

/* patterns. */

typedef struct compiled {
  char *pattern;
  bool ok;
  regex_t re;
} compiled;

const void *cima__regex_of(const char *pattern) {
  compiled *c = cima__cache_get(pattern, nullptr, CIMA__TAG_REGEX);
  if (LIKELY(c && strcmp(c->pattern, pattern) == 0)) return c->ok ? &c->re : nullptr;
  bool stale = c != nullptr;
  c = must_malloc(sizeof *c);
  c->pattern = must_malloc(strlen(pattern) + 1);
  strcpy(c->pattern, pattern);
  c->ok = regcomp(&c->re, pattern, REG_EXTENDED | REG_NOSUB) == 0;
  // a pattern at an address that held another one replaces it.
  compiled *won = cima__cache_put(pattern, nullptr, CIMA__TAG_REGEX, c, stale);
  if (won != c) {
    if (c->ok) regfree(&c->re);
    free(c->pattern);
    free(c);
  }
  return won->ok ? &won->re : nullptr;
}

bool cima__regex_match(const void *regex, const char *s) { return regexec(regex, s, 0, nullptr, 0) == 0; }
