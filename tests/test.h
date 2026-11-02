#ifndef CIMA_TEST_H
#define CIMA_TEST_H

#include <cilo/dsl.h>
#include <cima/dsl.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                                              \
  do {                                                                                                           \
    if (!(cond)) {                                                                                               \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                                                 \
      failures++;                                                                                                \
    }                                                                                                            \
  } while (0)

#define CHECK_STR(got, want)                                                                                     \
  do {                                                                                                           \
    const char *got_ = (got), *want_ = (want);                                                                   \
    if (!got_ || strcmp(got_, want_) != 0) {                                                                     \
      fprintf(stderr, "%s:%d: %s\n  got:  %s\n  want: %s\n", __FILE__, __LINE__, #got, got_ ? got_ : "(null)",   \
              want_);                                                                                            \
      failures++;                                                                                                \
    }                                                                                                            \
  } while (0)

// the first issue of a failed result, as "path: message".
static inline const char *first_issue(cilo_arena *a, cima_result r) {
  if (r.ok || !r.issues.len) return "ok";
  return cilo_printf(CILO_OF(a), "%s: %s", r.issues.items[0].path, r.issues.items[0].message);
}

static inline int report_failures(void) {
  if (failures) fprintf(stderr, "%d failed\n", failures);
  return failures != 0;
}

#endif
