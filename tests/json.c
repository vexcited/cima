#include "test.h"

#include <stdlib.h>

object(User, u32() id, string() handle, optional(string()) email, options(rename_all(CAMEL)));

static cima_json *parse(cilo_arena *a, const char *text) { return json_parse(a, text, strlen(text), nullptr); }

static const char doc_text[] =
    "{\"name\": \"vektra\", \"count\": 3, \"ratio\": 0.25, \"whole\": 4.0, \"big\": 18446744073709551615, "
    "\"neg\": -7, \"on\": true, \"none\": null, \"tags\": [\"a\", \"b\"], \"a/b~c\": {\"deep\": [10, 20]}}";

static void reading(void) {
  with_arena(a) {
    cima_json *person = parse(a, "{\"name\": \"ana\", \"age\": 31, \"nick\": null}");
    CHECK_STR(json_get_or(person, "name", nullptr), "ana");
    CHECK(json_get_or(person, "nick", nullptr) == nullptr && json_get_or(person, "age", nullptr) == nullptr);
    CHECK(json_get_or(person, "age", 0) == 31 && json_get_or(person, "gone", 7) == 7);
    CHECK_STR(json_get_or(person, "nick", "?"), "?");
    cima_json *loose = parse(a, "{\"at\": \"1795096047115\", \"big\": \"18446744073709551615\", \"half\": \"2.5\", "
                                "\"on\": \"yes\", \"one\": 1, \"word\": \"x\", \"real\": 4}");
    CHECK(json_get_coerce(loose, "at", (int64_t)0) == 1795096047115 && json_get_or(loose, "at", (int64_t)0) == 0);
    CHECK(json_get_coerce(loose, "big", (uint64_t)0) == UINT64_MAX && json_get_coerce(loose, "big", 7) == 7);
    CHECK(json_get_coerce(loose, "half", 0.0) == 2.5 && json_get_coerce(loose, "half", 9) == 9);
    CHECK(json_get_coerce(loose, "on", false) && json_get_coerce(loose, "one", false));
    CHECK(json_get_coerce(loose, "word", 3) == 3 && json_get_coerce(loose, "real", 0) == 4);
    cima_json *doc = json_parse(a, doc_text, sizeof doc_text - 1, nullptr);
    CHECK(doc && json_type(doc) == CIMA_JSON_OBJECT && json_len(doc) == 10);
    CHECK_STR(json_or(json_get(doc, "name"), "?"), "vektra");
    CHECK(json_or(json_get(doc, "count"), 0) == 3);
    CHECK(json_or(json_get(doc, "ratio"), 0.0) == 0.25);
    CHECK(json_or(json_get(doc, "count"), 0.0) == 3.0);
    CHECK(json_or(json_get(doc, "whole"), 0) == 4);
    CHECK(json_or(json_get(doc, "ratio"), -1) == -1);
    CHECK(json_or(json_get(doc, "big"), 0ull) == UINT64_MAX);
    CHECK(json_or(json_get(doc, "big"), 5) == 5);
    CHECK(json_or(json_get(doc, "neg"), 0ull) == 0);
    CHECK(json_or(json_get(doc, "on"), false) == true);
    CHECK(json_or(json_get(doc, "missing"), 25) == 25);
    CHECK(cima_json_is_null(json_get(doc, "none")) && !cima_json_is_null(json_get(doc, "missing")));
    CHECK(json_type(json_get(doc, "missing")) == CIMA_JSON_NULL);
    // chained reads through missing nodes need no checks.
    CHECK(json_or(json_get(json_get(json_get(doc, "nope"), "deeper"), "x"), 9) == 9);
    CHECK_STR(json_or(json_at(doc, "/tags/1"), ""), "b");
    CHECK(json_or(json_at(doc, "/a~1b~0c/deep/1"), 0) == 20);
    CHECK(json_at(doc, "") == doc);
    CHECK(json_at(doc, "/tags/01") == nullptr && json_at(doc, "/tags/-") == nullptr && json_at(doc, "tags") == nullptr);
    CHECK(json_at(doc, "/tags/2") == nullptr && json_index(json_get(doc, "tags"), 0)->len == 1);
    size_t n = 0;
    each(tag, json_items(json_get(doc, "tags"))) n += tag->len;
    CHECK(n == 2);
    n = 0;
    each(m, json_members(doc)) n += cima_json_key_len(m);
    CHECK(n == strlen("namecountratiowholebignegonnonetagsa/b~c"));
    each(m, json_members(json_get(doc, "tags"))) CHECK(!"an array has no members");
  }
}

static void changing(void) {
  with_arena(a) {
    cima_json *doc = parse(a, "{\"b\": 1}");
    json_set(a, doc, "a", "first");
    json_set(a, doc, "b", 2);
    json_insert(a, doc, 0, "z", json_arr(1, 2));
    CHECK_STR(json_text(a, doc), "{\"z\":[1,2],\"b\":2,\"a\":\"first\"}");
    // inserting an existing key moves it.
    json_insert(a, doc, 1, "a", nullptr);
    CHECK_STR(json_text(a, doc), "{\"z\":[1,2],\"a\":null,\"b\":2}");
    CHECK(json_remove(doc, "z") && !json_remove(doc, "z"));
    CHECK_STR(json_text(a, doc), "{\"a\":null,\"b\":2}");
    cima_json *list = json_set(a, doc, "list", json_arr());
    for (int i = 0; i < 100; i++) json_append(a, list, i);
    CHECK(json_len(list) == 100 && json_or(json_index(list, 99), 0) == 99);
    CHECK(json_remove_at(list, 0) && json_len(list) == 99 && json_or(json_index(list, 0), -1) == 1);
    CHECK(!json_remove_at(list, 99));
    for (int i = 0; i < 40; i++) {
      char key[8];
      snprintf(key, sizeof key, "k%d", i);
      json_set(a, doc, key, i);
    }
    CHECK(json_len(doc) == 43 && json_or(json_get(doc, "k39"), 0) == 39);
    // keys and values are copied, the caller's buffers may change.
    char key[] = "temp", value[] = "value";
    json_set(a, doc, key, value);
    key[0] = value[0] = 'X';
    CHECK_STR(json_or(json_get(doc, "temp"), ""), "value");
    // a key that needs escaping stays escaped.
    json_set(a, doc, "q\"\n", 1);
    CHECK(strstr(json_text(a, doc), "\"q\\\"\\n\":1") != nullptr);
    CHECK(!json_set(a, list, "x", 1) && !json_append(a, doc, 1));
  }
}

static void pushing(void) {
  with_arena(a) {
    // members appended in order without a lookup, onto a parsed object, a builder and a fresh one.
    cima_json *doc = parse(a, "{\"a\": 1}");
    json_obj_push(a, doc, "b", 2);
    json_obj_push(a, doc, "c", json_arr("x"));
    CHECK_STR(json_text(a, doc), "{\"a\":1,\"b\":2,\"c\":[\"x\"]}");
    cima_json *built = json_obj("id", 7);
    json_obj_push(a, built, "name", "ada");
    CHECK_STR(json_text(a, built), "{\"id\":7,\"name\":\"ada\"}");
    cima_json *big = json_clone(a, json_obj());
    char **keys = new_array(a, char *, 1000);
    for (int i = 0; i < 1000; i++) {
      keys[i] = aprintf(a, "k%d", i);
      json_obj_push(a, big, keys[i], i);
    }
    CHECK(json_len(big) == 1000 && json_or(json_get(big, "k0"), -1) == 0 && json_or(json_get(big, "k999"), -1) == 999);
    // the key is borrowed, not copied.
    CHECK(json_members(big).items[5].key == keys[5]);
    // a repeated key reads as the last one, and both are written.
    json_obj_push(a, doc, "b", 3);
    CHECK(json_or(json_get(doc, "b"), 0) == 3);
    CHECK_STR(json_text(a, doc), "{\"a\":1,\"b\":2,\"c\":[\"x\"],\"b\":3}");
    // a key that needs escaping stays escaped.
    json_obj_push(a, doc, "q\"", true);
    CHECK(strstr(json_text(a, doc), "\"q\\\"\":true") != nullptr);
    CHECK(!json_obj_push(a, json_arr(), "x", 1) && !json_obj_push(a, nullptr, "x", 1));
  }
}

static void building(void) {
  with_arena(a) {
    User user = {.id = 7, .handle = "ada"};
    cima_json *parsed = parse(a, "{\"x\": [1, 2]}");
    const char *missing = nullptr;
    unsigned char small = 200;
    long long wide = -9000000000;
    cima_json *body = json_obj(
        "error", false,
        "status", 200,
        "meta", json_obj(
            "user", json_typed(User, &user),
            "list", json_arr("a", 1, 2.5, nullptr, true, small, wide),
            "parsed", parsed,
            "raw", json_raw("{\"k\": [1]}"),
            "missing", missing,
            "empty", json_arr(),
            "nothing", json_obj(),
        ),
        "i18n", nullptr,
    );
    CHECK_STR(json_text(a, body), "{\"error\":false,\"status\":200,\"meta\":{\"user\":{\"id\":7,\"handle\":\"ada\"},"
                                  "\"list\":[\"a\",1,2.5,null,true,200,-9000000000],\"parsed\":{\"x\":[1,2]},"
                                  "\"raw\":{\"k\": [1]},\"missing\":null,\"empty\":[],\"nothing\":{}},\"i18n\":null}");
    CHECK(json_or(json_at(body, "/meta/list/5"), 0) == 200);

    // a builder kept past its block is copied, typed values become their json.
    cima_json *kept;
    {
      char name[16] = "temp";
      User other = {.id = 8, .handle = "linus", .email = {true, "l@k.org"}};
      kept = json_clone(a, json_obj("name", name, "user", json_typed(User, &other), "raw", json_raw("[1]")));
      memset(name, 'x', sizeof name - 1);
      other.handle = "changed";
    }
    CHECK_STR(json_text(a, kept), "{\"name\":\"temp\",\"user\":{\"id\":8,\"handle\":\"linus\",\"email\":\"l@k.org\"},"
                                  "\"raw\":[1]}");
    CHECK(json_type(json_get(kept, "user")) == CIMA_JSON_RAW);

    // storing a builder value copies it as well.
    cima_json *holder = json_clone(a, json_obj());
    {
      char text[8] = "in";
      json_set(a, holder, "inner", json_obj("text", text, "n", json_arr(1)));
      text[0] = 'X';
    }
    CHECK_STR(json_text(a, holder), "{\"inner\":{\"text\":\"in\",\"n\":[1]}}");
    // setting on a builder object moves it into the arena first.
    cima_json *live = json_obj("a", 1);
    json_set(a, live, "b", 2);
    CHECK_STR(json_text(a, live), "{\"a\":1,\"b\":2}");
  }
}

static void writing(void) {
  with_arena(a) {
    cima_json_writer early = json_writer(a);
    CHECK_STR(cima_json_writer_text(&early), "");
    json_begin_array(&early);
    json_put(&early, 1);
    CHECK_STR(cima_json_writer_text(&early), "[1");
    json_end_array(&early);
    CHECK_STR(cima_json_writer_text(&early), "[1]");
    User user = {.id = 1, .handle = "x\ty"};
    cima_json_writer w = json_writer(a);
    json_begin_object(&w);
    json_field(&w, "null", nullptr);
    json_field(&w, "bool", true);
    json_field(&w, "int", -5);
    json_field(&w, "uint", UINT64_MAX);
    json_field(&w, "double", 2.5);
    json_field(&w, "string", "q\"\\/\x01\xe2\x82\xac");
    json_key(&w, "raw");
    cima_json_write_raw(&w, "[1]", 3);
    json_key(&w, "typed");
    json_put_typed(&w, User, &user);
    json_key(&w, "array");
    json_begin_array(&w);
    json_put(&w, 1);
    json_put(&w, json_obj("k", "v"));
    json_begin_array(&w);
    json_end_array(&w);
    json_end_array(&w);
    json_end_object(&w);
    size_t len;
    char *text = cima_json_writer_finish(&w, &len);
    CHECK_STR(text, "{\"null\":null,\"bool\":true,\"int\":-5,\"uint\":18446744073709551615,\"double\":2.5,\"string\":"
                    "\"q\\\"\\\\/\\u0001\xe2\x82\xac\",\"raw\":[1],\"typed\":{\"id\":1,\"handle\":\"x\\ty\"},\"array\":"
                    "[1,{\"k\":\"v\"},[]]}");
    CHECK(len == strlen(text) && cima_json_valid(text, len));
    // the writer is ready again.
    json_put(&w, "again");
    CHECK_STR(json_finish(&w), "\"again\"");

    cima_json *doc = parse(a, "{\"a\": 1, \"b\": [true, {}], \"c\": [], \"d\": {\"e\": null}}");
    CHECK_STR(json_pretty(a, doc), "{\n  \"a\": 1,\n  \"b\": [\n    true,\n    {}\n  ],\n  \"c\": [],\n  \"d\": {\n"
                                   "    \"e\": null\n  }\n}");
    cima_json_writer p = json_writer(a, .indent = 4);
    json_put_typed(&p, User, &user);
    CHECK_STR(json_finish(&p), "{\n    \"id\": 1,\n    \"handle\": \"x\\ty\"\n}");
    // the heap works too.
    cima_json_writer h = {};
    json_put(&h, json_arr(1, 2));
    char *on_heap = json_finish(&h);
    CHECK_STR(on_heap, "[1,2]");
    cilo_mfree(on_heap);
  }
}

static void comparing(void) {
  with_arena(a) {
    cima_json *x = parse(a, "{\"a\": [1, 2.0, {\"b\": null}], \"c\": \"d\"}");
    cima_json *y = parse(a, "{\"c\": \"d\", \"a\": [1.0, 2, {\"b\": null}]}");
    CHECK(json_eq(x, y));
    CHECK(json_eq(x, json_obj("c", "d", "a", json_arr(1, 2, json_obj("b", nullptr)))));
    CHECK(json_eq(x, json_raw("{\"c\": \"d\", \"a\": [1, 2, {\"b\": null}]}")));
    CHECK(!json_eq(x, json_obj("c", "d", "a", json_arr(2, 1, json_obj("b", nullptr)))));
    CHECK(!json_eq(x, json_obj("c", "d")));
    CHECK(!json_eq(json_of(1), json_of("1")));
    CHECK(!json_eq(json_of(0.5), json_of(0)));
    CHECK(json_eq(json_of(UINT64_MAX), parse(a, "18446744073709551615")));
    CHECK(!json_eq(json_of(-1), json_of(UINT64_MAX)));
    User u = {.id = 3, .handle = "h"};
    CHECK(json_eq(json_typed(User, &u), json_obj("id", 3, "handle", "h")));
    // big objects take the indexed path.
    cima_json *big1 = json_clone(a, json_obj()), *big2 = json_clone(a, json_obj());
    for (int i = 0; i < 50; i++) {
      char key[8];
      snprintf(key, sizeof key, "%d", i);
      json_set(a, big1, key, i);
      snprintf(key, sizeof key, "%d", 49 - i);
      json_set(a, big2, key, 49 - i);
    }
    CHECK(json_eq(big1, big2));
    json_set(a, big2, "7", 8);
    CHECK(!json_eq(big1, big2));
  }
}

/* livegraph's diff.rs, written with the diff helpers. */

#define TOMBSTONE "_denormUpsertIsTombstone_"

// an object new to the view says it is alive.
static cima_json *fresh(cilo_arena *a, const cima_json *obj) {
  if (json_type(obj) != CIMA_JSON_OBJECT || json_get(obj, TOMBSTONE)) return (cima_json *)obj;
  cima_json *copy = json_clone(a, obj);
  json_set(a, copy, TOMBSTONE, false);
  return copy;
}

static cima_json *removal(cilo_arena *a, const char *key, const cima_json *old, const cima_json *now_ids) {
  cima_json *id = json_get(old, "id");
  if (json_get(now_ids, key)) return json_clone(a, json_obj("id", id ? id : json_of(key)));
  return json_clone(a, json_obj("id", id ? id : json_of(key), TOMBSTONE, true));
}

static cima_json *diff_query(cilo_arena *a, const cima_json *old, const cima_json *now, const cima_json *now_ids) {
  cima_json *kinds = json_clone(a, json_obj());
  cima_json_changes changes = json_diff(a, old, now);
  if (changes.reordered) {
    cima_json *initial = json_set(a, kinds, "initial", json_obj());
    each(m, json_members(now)) {
      cima_json *before = json_get(old, m->key);
      json_set(a, initial, m->key, before ? json_patch(a, before, &m->value) : fresh(a, &m->value));
    }
  } else {
    cima_json *update = json_clone(a, json_obj());
    each(c, changes) {
      if (c->kind == CIMA_JSON_ADDED) json_set(a, update, c->key, fresh(a, c->after));
      if (c->kind == CIMA_JSON_CHANGED) json_set(a, update, c->key, json_patch(a, c->before, c->after));
    }
    if (json_len(update)) json_set(a, kinds, "update", update);
  }
  cima_json *removed = json_clone(a, json_obj());
  each(c, changes) {
    if (c->kind == CIMA_JSON_REMOVED) json_set(a, removed, c->key, removal(a, c->key, c->before, now_ids));
  }
  if (json_len(removed)) json_set(a, kinds, "delete", removed);
  return json_len(kinds) ? kinds : nullptr;
}

static void diffing(void) {
  with_arena(a) {
    // changed_new_and_removed.
    cima_json *old = parse(a, "{\"1\": {\"id\": \"1\", \"name\": \"a\", \"x\": 1}, "
                              "\"2\": {\"id\": \"2\", \"name\": \"b\"}}");
    cima_json *now = parse(a, "{\"1\": {\"id\": \"1\", \"name\": \"c\"}, \"3\": {\"id\": \"3\", \"name\": \"d\"}}");
    cima_json *d = diff_query(a, old, now, now);
    CHECK(json_eq(d, parse(a, "{\"update\": {\"1\": {\"id\": \"1\", \"name\": \"c\", \"x\": null}, "
                              "\"3\": {\"id\": \"3\", \"name\": \"d\", \"" TOMBSTONE "\": false}}, "
                              "\"delete\": {\"2\": {\"id\": \"2\", \"" TOMBSTONE "\": true}}}")));
    CHECK(diff_query(a, old, old, old) == nullptr);

    // moved_between_queries_is_not_tombstoned.
    cima_json *ids = parse(a, "{\"1\": true}");
    d = diff_query(a, parse(a, "{\"1\": {\"id\": \"1\"}}"), parse(a, "{}"), ids);
    CHECK(json_eq(json_at(d, "/delete/1"), parse(a, "{\"id\": \"1\"}")));

    // reorder_resends_the_query.
    d = diff_query(a, parse(a, "{\"1\": {\"id\": \"1\"}, \"2\": {\"id\": \"2\"}}"),
                   parse(a, "{\"2\": {\"id\": \"2\"}, \"1\": {\"id\": \"1\"}}"), nullptr);
    cima_json *initial = json_get(d, "initial");
    CHECK(json_len(initial) == 2 && strcmp(initial->members[0].key, "2") == 0);

    // reordered_objects_forget_lost_fields.
    d = diff_query(a, parse(a, "{\"1\": {\"id\": \"1\", \"x\": 1}, \"2\": {\"id\": \"2\", \"x\": 2}}"),
                   parse(a, "{\"2\": {\"id\": \"2\"}, \"1\": {\"id\": \"1\", \"x\": 1}}"), nullptr);
    CHECK(json_eq(json_at(d, "/initial/2"), parse(a, "{\"id\": \"2\", \"x\": null}")));
    CHECK(json_eq(json_at(d, "/initial/1"), parse(a, "{\"id\": \"1\", \"x\": 1}")));

    // the raw changes, on objects big enough for the indexed path.
    cima_json *before = json_clone(a, json_obj()), *after = json_clone(a, json_obj());
    for (int i = 0; i < 30; i++) {
      char key[8];
      snprintf(key, sizeof key, "%d", i);
      if (i != 3) json_set(a, before, key, i);
      if (i != 4) json_set(a, after, key, i == 5 ? -1 : i);
    }
    cima_json_changes changes = json_diff(a, before, after);
    CHECK(!changes.reordered && changes.len == 3);
    CHECK(changes.items[0].kind == CIMA_JSON_ADDED && strcmp(changes.items[0].key, "3") == 0);
    CHECK(changes.items[1].kind == CIMA_JSON_CHANGED && json_or(changes.items[1].before, 0) == 5);
    CHECK(changes.items[2].kind == CIMA_JSON_REMOVED && strcmp(changes.items[2].key, "4") == 0);
    CHECK(json_diff(a, nullptr, nullptr).len == 0 && json_diff(a, nullptr, after).len == 29);
    // a patch of things that are not two objects is the new value.
    CHECK(json_patch(a, json_of(1), after) == after);
  }
}

// the parser keeps strings in one copy of the text, which escapes, nesting and big documents must not disturb.
static void parsing(void) {
  with_arena(a) {
    const char text[] = "{\"k\\\"ey\": \"a\\nb\\u00e9\\ud83d\\ude00\", \"plain\": \"xyz\", \"\": \"\\u0000z\"}";
    cima_json *doc = parse(a, text);
    CHECK_STR(json_or(json_get(doc, "k\"ey"), ""), "a\nb\xc3\xa9\xf0\x9f\x98\x80");
    CHECK(json_get(doc, "k\"ey")->len == 9 && !(json_get(doc, "k\"ey")->flags & CIMA_JSON__CLEAN));
    CHECK_STR(json_or(json_get(doc, "plain"), ""), "xyz");
    CHECK(json_get(doc, "plain")->flags & CIMA_JSON__CLEAN);
    CHECK(json_get(doc, "")->len == 2 && json_get(doc, "")->str[1] == 'z');
    // the caller's text is left alone.
    CHECK(text[3] == '\\');
    CHECK_STR(json_text(a, doc), "{\"k\\\"ey\":\"a\\nb\xc3\xa9\xf0\x9f\x98\x80\",\"plain\":\"xyz\",\"\":\"\\u0000z\"}");

    // integers on the short path and past it.
    cima_json *nums = parse(a, "[7, 123456789012345678, 1234567890123456789, 12345678901234567890, 1.5, 2e3, -0, 0]");
    CHECK(json_index(nums, 0)->type == CIMA_JSON_INT && json_index(nums, 0)->i == 7);
    CHECK(json_index(nums, 1)->i == 123456789012345678);
    CHECK(json_index(nums, 2)->i == 1234567890123456789);
    CHECK(json_index(nums, 3)->type == CIMA_JSON_UINT && json_index(nums, 3)->u == 12345678901234567890u);
    CHECK(json_index(nums, 4)->type == CIMA_JSON_DOUBLE && json_index(nums, 4)->d == 1.5);
    CHECK(json_index(nums, 5)->type == CIMA_JSON_DOUBLE && json_index(nums, 5)->d == 2000);
    CHECK(json_index(nums, 6)->type == CIMA_JSON_INT && json_index(nums, 6)->i == 0);
    cima_json_error err;
    CHECK(!json_parse(a, "[12x]", 5, &err) && err.offset == 3);
    CHECK(!json_parse(a, "[1", 2, &err) && err.offset == 2);

    // deep nesting, stacks that grow many times, and containers closing in every order.
    cima_json_writer w = json_writer(a, .indent = 1);
    json_begin_array(&w);
    for (int i = 0; i < 20000; i++) {
      json_begin_object(&w);
      json_field(&w, "i", i);
      json_key(&w, "list");
      json_begin_array(&w);
      for (int j = 0; j < i % 5; j++) json_put(&w, json_arr(j, "s"));
      json_end_array(&w);
      json_end_object(&w);
    }
    json_end_array(&w);
    size_t len;
    char *big = cima_json_writer_finish(&w, &len);
    cima_json *all = json_parse(a, big, len, nullptr);
    CHECK(json_len(all) == 20000);
    CHECK(json_or(json_at(all, "/19999/i"), 0) == 19999 && json_len(json_at(all, "/19999/list")) == 4);
    CHECK_STR(json_or(json_at(all, "/7/list/1/1"), ""), "s");
    char deep[1100];
    for (int i = 0; i < 512; i++) deep[i] = '[', deep[1023 - i] = ']';
    CHECK(json_parse(a, deep, 1024, nullptr) != nullptr);
    deep[1024] = ']';
    memmove(deep + 1, deep, 1024);
    deep[0] = '[';
    CHECK(!json_parse(a, deep, 1025, &err) && strcmp(err.message, "nested too deep") == 0 && err.offset == 512);

    // a failed parse gives back what it took from the arena.
    cilo_mark before = cilo_mark_of(a);
    CHECK(!json_parse(a, big, len - 1, &err) && err.offset == len - 1);
    CHECK(a->cur == before.cur && a->chunk == before.chunk);
  }
}

static void formatting(void) {
  char out[CIMA_JSON_NUMBER_MAX];
  CHECK(json_format(out, 0.1) == 3 && strcmp(out, "0.1") == 0);
  json_format(out, -0.0);
  CHECK_STR(out, "-0");
  json_format(out, 1e300);
  CHECK_STR(out, "1e+300");
  json_format(out, 5e-324);
  CHECK_STR(out, "5e-324");
  json_format(out, 1.7976931348623157e308);
  CHECK_STR(out, "1.7976931348623157e+308");
  json_format(out, 2.0f);
  CHECK_STR(out, "2");
  json_format(out, 0.0 / 0.0);
  CHECK_STR(out, "null");
  CHECK(json_format(out, INT64_MIN) == 20 && strcmp(out, "-9223372036854775808") == 0);
  json_format(out, UINT64_MAX);
  CHECK_STR(out, "18446744073709551615");
  json_format(out, -42);
  CHECK_STR(out, "-42");
  json_format(out, (unsigned char)200);
  CHECK_STR(out, "200");
  // every double reads back the same, and fits.
  uint64_t seed = 42;
  bool exact = true;
  for (int i = 0; i < 100000; i++) {
    seed = seed * 6364136223846793005u + 1442695040888963407u;
    double d;
    memcpy(&d, &seed, 8);
    if (d != d) continue;
    size_t n = cima_json_format_double(out, d);
    exact &= n < CIMA_JSON_NUMBER_MAX && strtod(out, nullptr) == d;
  }
  CHECK(exact);
}

// random strings mixing ascii runs, escapes and utf-8, decoded by the dom into a copy that moves as it reads:
// every shift between what is read and what is written comes up, and the bytes must come out as written.
static uint64_t rng_state = 0x9e3779b97f4a7c15ull;

static uint32_t rng(uint32_t n) {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return (uint32_t)(rng_state % n);
}

static char *put_utf8(char *d, uint32_t u) {
  if (u < 0x80) {
    *d++ = (char)u;
  } else if (u < 0x800) {
    *d++ = (char)(0xc0 | u >> 6);
    *d++ = (char)(0x80 | (u & 0x3f));
  } else if (u < 0x10000) {
    *d++ = (char)(0xe0 | u >> 12);
    *d++ = (char)(0x80 | ((u >> 6) & 0x3f));
    *d++ = (char)(0x80 | (u & 0x3f));
  } else {
    *d++ = (char)(0xf0 | u >> 18);
    *d++ = (char)(0x80 | ((u >> 12) & 0x3f));
    *d++ = (char)(0x80 | ((u >> 6) & 0x3f));
    *d++ = (char)(0x80 | (u & 0x3f));
  }
  return d;
}

static uint32_t random_code_point(void) {
  switch (rng(4)) {
  case 0: return 0x80 + rng(0x800 - 0x80);
  case 1: return 0x3040 + rng(0x60);
  case 2: {
    uint32_t u = 0x800 + rng(0x10000 - 0x800);
    return u >= 0xd800 && u <= 0xdfff ? 0xe000 : u;
  }
  default: return 0x10000 + rng(0x110000 - 0x10000);
  }
}

// one string body into `text` and what it decodes to into `want`.
static void random_body(char **text, char **want) {
  char *t = *text, *w = *want;
  for (uint32_t pieces = rng(12); pieces--;) {
    switch (rng(6)) {
    case 0:
    case 1:
      for (uint32_t n = rng(3) ? rng(10) : 16 + rng(40); n--;) {
        char c = (char)(' ' + rng(95));
        if (c == '"' || c == '\\') c = 'x';
        *t++ = *w++ = c;
      }
      break;
    case 2: {
      static const char escapes[] = "\"\\/bfnrt", decoded[] = "\"\\/\b\f\n\r\t";
      uint32_t i = rng(8);
      *t++ = '\\';
      *t++ = escapes[i];
      *w++ = decoded[i];
      break;
    }
    case 3: {
      uint32_t u = rng(5) ? random_code_point() : rng(0x80);
      if (u >= 0x10000) {
        uint32_t v = u - 0x10000;
        t += snprintf(t, 16, "\\u%04X\\u%04x", 0xd800 + (v >> 10), 0xdc00 + (v & 0x3ff));
      } else {
        t += snprintf(t, 8, "\\u%04x", u);
      }
      w = put_utf8(w, u);
      break;
    }
    default:
      for (uint32_t n = 1 + rng(8); n--;) {
        uint32_t u = random_code_point();
        t = put_utf8(t, u);
        w = put_utf8(w, u);
      }
      break;
    }
  }
  *text = t;
  *want = w;
}

static void strings_at_random(void) {
  with_arena(a) {
    for (int round = 0; round < 20000; round++) {
      cilo_mark mark = cilo_mark_of(a);
      char *text = cilo_arena_alloc(a, 8192, 1), *t = text;
      char *wants[8], *ends[8];
      uint32_t count = 1 + rng(8);
      *t++ = '{';
      for (uint32_t i = 0; i < count; i++) {
        if (i) *t++ = ',';
        if (rng(2)) t += snprintf(t, 16, "\n%*s", (int)rng(9), "");
        t += snprintf(t, 32, "\"k%u\": \"", i);
        wants[i] = ends[i] = cilo_arena_alloc(a, 1024, 1);
        random_body(&t, &ends[i]);
        *t++ = '"';
      }
      *t++ = '}';
      size_t len = (size_t)(t - text);
      // one byte made invalid in some of them.
      size_t broken = rng(4) ? SIZE_MAX : rng((uint32_t)len);
      static const unsigned char bad[] = {0xff, 0x80, 0xc0, 0x01, 0xed, 0xf5};
      if (broken != SIZE_MAX) text[broken] = (char)bad[rng(sizeof bad)];
      cima_json *doc = json_parse(a, text, len, nullptr);
      if (broken == SIZE_MAX) {
        CHECK(doc && json_len(doc) == count && cima_json_valid(text, len));
        for (uint32_t i = 0; doc && i < count; i++) {
          cima_json *s = &json_members(doc).items[i].value;
          size_t n = (size_t)(ends[i] - wants[i]);
          bool same = s->type == CIMA_JSON_STRING && s->len == n && memcmp(s->str, wants[i], n) == 0 &&
                      s->str[n] == '\0';
          if (!same) fprintf(stderr, "round %d, string %u: %.*s\n", round, i, (int)len, text);
          CHECK(same);
        }
      } else if (doc) {
        // the broken byte may have landed somewhere it is valid.
        CHECK(cima_json_valid(text, len));
      } else {
        CHECK(!cima_json_valid(text, len));
      }
      cilo_rewind(mark);
    }
  }
}

// integers written by the dom writer, every digit count and both signs, against snprintf.
static void integers_written(void) {
  with_arena(a) {
    for (int i = 0; i < 200000; i++) {
      uint64_t u = ((uint64_t)rng(1u << 31) << 33) ^ ((uint64_t)rng(1u << 31) << 2) ^ rng(4);
      u >>= rng(64);
      if (i < 64) u = i < 20 ? (uint64_t)1 << (3 * i) : UINT64_MAX >> (i - 20);
      char want[32];
      snprintf(want, sizeof want, "%llu", (unsigned long long)u);
      CHECK_STR(json_text(a, json_of(u)), want);
      int64_t s = -(int64_t)(u >> 1);
      snprintf(want, sizeof want, "%lld", (long long)s);
      CHECK_STR(json_text(a, json_of(s)), want);
      for (uint64_t p = 1; i == 0 && p; p = p <= UINT64_MAX / 10 ? p * 10 : 0) {
        for (int k = -1; k <= 1; k++) {
          snprintf(want, sizeof want, "%llu", (unsigned long long)(p + (uint64_t)k));
          CHECK_STR(json_text(a, json_of(p + (uint64_t)k)), want);
        }
      }
    }
  }
}

int main(void) {
  parsing();
  strings_at_random();
  integers_written();
  formatting();
  reading();
  changing();
  pushing();
  building();
  writing();
  comparing();
  diffing();
  return report_failures();
}
