#include "test.h"

#include <pthread.h>
#include <stdlib.h>

object(Item, string() name, array(u16()) sizes, optional(bytes()) blob);

object(Order,
  u64()                                     id,
  string(trim(), lowercase())               customer,
  array(Item, nonempty())                   items,
  array(string(), fallback(["standard"]))   flags,
  unknown()                                 meta,
  nullable(string(matches("^[A-Z]{2}$")))   country,
);

static const char order_json[] = "{\"id\": 9, \"customer\": \"  ACME \", \"items\": [{\"name\": \"bolt\", "
                                 "\"sizes\": [1, 2, 3], \"blob\": \"AAAA\"}, {\"name\": \"nut\\u00e9\", "
                                 "\"sizes\": []}], \"meta\": {\"k\": [true]}, \"country\": \"FR\"}";

// an allocator that knows every live block and checks the size each free gives back.
typedef struct counting {
  cilo_allocator allocator;
  size_t live, bytes, bad_frees;
  void *ptrs[4096];
  size_t sizes[4096];
} counting;

static void *counting_resize(cilo_allocator *self, void *ptr, size_t old_size, size_t new_size, size_t align) {
  counting *c = (counting *)self;
  (void)align;
  if (ptr) {
    size_t i = 0;
    while (i < c->live && c->ptrs[i] != ptr) i++;
    if (i == c->live || c->sizes[i] != old_size) c->bad_frees++;
    if (i < c->live) {
      c->bytes -= c->sizes[i];
      c->ptrs[i] = c->ptrs[--c->live];
      c->sizes[i] = c->sizes[c->live];
    }
  }
  if (!new_size) {
    free(ptr);
    return nullptr;
  }
  void *p = realloc(ptr, new_size);
  c->ptrs[c->live] = p;
  c->sizes[c->live++] = new_size;
  c->bytes += new_size;
  return p;
}

static const cilo_vtable counting_vtable = {counting_resize};

static void custom_allocator(void) {
  static counting c = {.allocator = &counting_vtable};
  cima_options o = {.alloc = &c.allocator};
  Order order;
  cima_result r = cima_parse_json(&Order_schema, order_json, sizeof order_json - 1, &order, &o);
  CHECK(r.ok && r.owner == nullptr);
  CHECK(c.live > 0);
  CHECK_STR(order.customer, "acme");
  CHECK_STR(order.items.items[1].name, "nut\xc3\xa9");
  CHECK(order.flags.len == 1 && order.flags.cap == 1);
  size_t len;
  char *text = cima_to_json(&Order_schema, &order, &o, &len);
  CHECK(c.sizes[c.live - 1] == len + 1);
  cilo_free(&c.allocator, text, len + 1);
  cima_dispose_with(&Order_schema, &order, &c.allocator);
  CHECK(c.live == 0 && c.bad_frees == 0);
  CHECK(order.items.items == nullptr && order.customer == nullptr);

  // a failed parse releases the value and leaves one block, the issues.
  const char *bad = "{\"id\": 1, \"customer\": \"x\", \"items\": [{\"name\": \"a\", \"sizes\": [1, 2]}, "
                    "{\"name\": 1}], \"meta\": null, \"country\": \"fr\"}";
  r = cima_parse_json(&Order_schema, bad, strlen(bad), &order, &o);
  CHECK(!r.ok && r.issues.len == 3 && c.live == 1 && r.owner == &c.allocator);
  CHECK(order.customer == nullptr && order.items.len == 0);
  cima_result_dispose(&r);
  CHECK(c.live == 0 && c.bad_frees == 0);

  // so does a syntax error in the middle of a list.
  const char *cut = "{\"id\": 1, \"customer\": \"x\", \"items\": [{\"name\": \"a\", \"sizes\": [1, ";
  r = cima_parse_json(&Order_schema, cut, strlen(cut), &order, &o);
  CHECK(!r.ok && c.live == 1);
  cima_result_dispose(&r);
  CHECK(c.live == 0 && c.bad_frees == 0);
}

static void heap(void) {
  Order order;
  cima_result r = cima_parse_json(&Order_schema, order_json, sizeof order_json - 1, &order, nullptr);
  CHECK(r.ok);
  CHECK(order.items.len == 2 && order.items.items[0].blob.some && order.items.items[0].blob.value.len == 3);
  char *text = cima_to_json(&Order_schema, &order, nullptr, nullptr);
  Order again;
  CHECK(cima_parse_json(&Order_schema, text, strlen(text), &again, nullptr).ok);
  char *twice = cima_to_json(&Order_schema, &again, nullptr, nullptr);
  CHECK_STR(twice, text);
  cilo_mfree(text);
  cilo_mfree(twice);
  Order_dispose(&order);
  Order_dispose(&again);
  r = cima_parse_json(&Order_schema, "{}", 2, &order, nullptr);
  CHECK(!r.ok && r.issues.len == 5 && r.owner != nullptr);
  cima_result_dispose(&r);
  CHECK(r.issues.items == nullptr);
}

static void arenas(void) {
  with_arena(a) {
    Order order;
    (void)new(a, char);
    char *before = a->cur;
    // a failure gives back everything but the issues.
    cima_result r = CIMA_PARSE(Order, "{\"id\": 1, \"customer\": \"a very long customer name that takes room\", "
                                      "\"items\": [], \"meta\": 1, \"country\": null}",
                               &order, .arena = a);
    CHECK(!r.ok && r.issues.len == 1 && r.owner == nullptr);
    CHECK_STR(r.issues.items[0].path, "/items");
    CHECK((size_t)(a->cur - before) < 96);
    CHECK(CIMA_PARSE(Order, order_json, &order, .arena = a).ok);
    // parsed lists own their items, push grows them.
    push(a, &order.flags, "express");
    CHECK(order.flags.len == 2 && strcmp(order.flags.items[0], "standard") == 0);
    push(a, &order.items, .name = "washer");
    CHECK(order.items.len == 3 && strcmp(order.items.items[1].name, "nut\xc3\xa9") == 0);
    // the default is copied, not shared between values.
    Order other;
    CHECK(CIMA_PARSE(Order, order_json, &other, .arena = a).ok);
    CHECK(other.flags.len == 1 && other.flags.items != order.flags.items);
  }

  // if_parsed rewinds its scratch arena, a loop never grows it.
  cilo_stats first = {};
  for (int i = 0; i < 1000; i++) {
    bool ok = false;
    if_parsed(Order, order, order_json) {
      ok = order.items.len == 2;
    } else {
      each(issue, issues) fprintf(stderr, "%s: %s\n", issue->path, issue->message);
    }
    CHECK(ok);
    if_parsed(Order, order, "{\"id\": -1}") {
      CHECK(!"parsed");
    } else {
      ok = issues.len == 5;
    }
    CHECK(ok);
    cilo_stats now = cilo_arena_stats(&cilo__scratch_own[0]);
    if (i == 0) first = now;
    else if (now.reserved != first.reserved) CHECK(!"scratch grew");
  }

  // with an arena, the value outlives the block.
  with_arena(kept) {
    const char *customer = nullptr;
    if_parsed(Order, order, order_json, kept) {
      customer = order.customer;
    }
    CHECK_STR(customer, "acme");
  }
}

static void *worker(void *arg) {
  int *failed = arg;
  for (int i = 0; i < 2000; i++) {
    with_arena(a) {
      Order order;
      cima_result r = CIMA_PARSE(Order, order_json, &order, .arena = a);
      if (!r.ok || order.flags.len != 1 || !order.country.some) ++*failed;
      char *text = CIMA_TO_JSON(Order, &order, .arena = a);
      if (!strstr(text, "\"flags\":[\"standard\"]")) ++*failed;
    }
  }
  return nullptr;
}

// the schema caches are built by whichever thread comes first.
static void threads(void) {
  pthread_t t[8];
  int failed[8] = {};
  for (int i = 0; i < 8; i++) pthread_create(&t[i], nullptr, worker, &failed[i]);
  for (int i = 0; i < 8; i++) {
    pthread_join(t[i], nullptr);
    CHECK(failed[i] == 0);
  }
}

int main(void) {
  custom_allocator();
  heap();
  arenas();
  threads();
  return report_failures();
}
