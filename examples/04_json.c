#include <cilo/dsl.h>
#include <cima/dsl.h>
#include <stdio.h>
#include <string.h>

object(File,
  string()            key,
  string()            name,
  nullable(string())  folder_id,
  timestamp()         created_at,
);

// figma's envelope, `{error, status, meta, i18n}`, around a typed value and ad hoc fields. no schema for the
// envelope, and no allocation for its nodes: they live until the end of the block.
static char *ok(cilo_arena *a, const File *file, bool can_edit) {
  return json_text(a, json_obj(
      "error", false,
      "status", 200,
      "meta", json_obj(
          "file", json_typed(File, file),
          "can_edit", can_edit,
          "thumbnail_url", nullptr,
      ),
      "i18n", nullptr,
  ));
}

int main(void) {
  with_arena(a) {
    File file = {.key = "aBc", .name = "Landing page", .created_at = 1793281630000};
    puts(ok(a, &file, true));

    // any json, read without a schema.
    const char *args = "{\"firstPageSize\": 50, \"resourceTypes\": [\"file\", \"repo\"]}";
    cima_json_error err;
    cima_json *doc = json_parse(a, args, strlen(args), &err);
    if (!doc) {
      fprintf(stderr, "%s at %zu\n", err.message, err.offset);
      return 1;
    }
    int64_t size = json_or(json_get(doc, "firstPageSize"), 25);
    bool wants_files = false;
    each(type, json_items(json_get(doc, "resourceTypes"))) wants_files |= strcmp(json_or(type, ""), "file") == 0;
    printf("page of %lld, files %s\n", (long long)size, wants_files ? "wanted" : "skipped");

    // a change for a client that merges shallowly: lost fields come back as null.
    const char *old = "{\"id\": \"1\", \"name\": \"a\", \"x\": 1}";
    cima_json *before = json_parse(a, old, strlen(old), nullptr);
    cima_json *after = json_clone(a, json_obj("id", "1", "name", "b"));
    cima_json_changes changes = json_diff(a, before, after);
    static const char *const kinds[] = {"", "added", "changed", "removed"};
    each(change, changes) printf("%s %s\n", kinds[change->kind], change->key);
    puts(json_text(a, json_patch(a, before, after)));

    // the streaming writer, for when the shape follows a loop.
    cima_json_writer w = json_writer(a, .indent = 2);
    json_begin_object(&w);
    json_key(&w, "files");
    json_begin_array(&w);
    for (int i = 0; i < 2; i++) json_put_typed(&w, File, &file);
    json_end_array(&w);
    json_field(&w, "total", 2);
    json_end_object(&w);
    puts(json_finish(&w));
  }
  return 0;
}
