#include <cilo/dsl.h>
#include <cima/dsl.h>
#include <stdio.h>

// object() is z.object() and z.infer at once. the fields read like a struct, type first, and it defines
// `struct User` and `User_schema`, plus User_list, User_maybe and User_dispose for free.
object(User,
  u32()                                                           id,
  string(trim(), at_least(1), at_most(64))                        name,
  string(email(), lowercase())                                    email,
  optional(u8(at_least(13)))                                      age,
  number(gt(0), lt(1), fallback(0.5))                             score,
  nullable(string(url()))                                         website,
  array(string(at_most(32)), at_most(8), unique(), fallback([]))  tags,
  timestamp(describe("when the account was created"))             joined_at,
);

int main(void) {
  const char *json = "{"
                     "  \"id\": 1,"
                     "  \"name\": \"  Ada  \","
                     "  \"email\": \"ADA@example.com\","
                     "  \"website\": null,"
                     "  \"tags\": [\"c\", \"zig\"],"
                     "  \"joined_at\": \"2026-10-29T13:47:10Z\""
                     "}";

  // z.safeParse as a block: `user` is typed and `issues` lists what went wrong. both live in a scratch arena,
  // freed with one store at the end of the block.
  if_parsed(User, user, json) {
    // trimmed, lowercased and defaulted by the schema.
    printf("%s <%s> score=%g\n", user.name, user.email, user.score);
    printf("age %u\n", value_or(user.age, 0));
    each(tag, user.tags)
      printf("tag %s\n", *tag);

    // encoding and typebox style json schema from the same definition, into a scratch arena too.
    scratch(tmp) {
      puts(CIMA_TO_JSON(User, &user, .arena = tmp));
      puts(CIMA_JSON_SCHEMA(User, .arena = tmp));
    }
  } else {
    each(issue, issues)
      fprintf(stderr, "%s: %s\n", issue->path, issue->message);
    return 1;
  }

  // a value built in c goes through the same checks.
  User built = {.id = 2, .name = "Linus", .email = "linus@example.com", .score = 0.9};
  built.tags = (cima_string_list){.items = (cima_string[]){"c", "c"}, .len = 2};
  // a list points at its items like this one, or grows with cilo's push.
  scratch(tmp) {
    cima_result check = CIMA_CHECK(User, &built, .arena = tmp);
    printf("duplicate tags rejected: %s\n", check.ok ? "no" : "yes");
  }
  return 0;
}
