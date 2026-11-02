#include <cilo/dsl.h>
#include <cima/dsl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// the file object of vektra's shape.rs, as GET /api/files and the listings answer it.
object(FileJson,
  string()             key,
  string()             id,
  string()             name,
  nullable(string())   description,
  nullable(string())   creator_id,
  nullable(string())   folder_id,
  nullable(string())   team_id,
  nullable(string())   parent_org_id,
  nullable(string())   org_id,
  string()             editor_type,
  string()             link_access,
  string()             proto_link_access,
  boolean()            has_file_link_password,
  boolean()            has_proto_link_password,
  boolean()            viewer_export_restricted,
  boolean()            org_audience,
  boolean()            org_browsable,
  boolean()            folder_access_enabled,
  string()             url,
  string()             edit_url,
  string()             prototype_url,
  string()             handoff_url,
  string()             slides_url,
  string()             thumbnail_url,
  nullable(string())   thumbnail_url_override,
  nullable(string())   thumbnail_guid,
  nullable(string())   client_meta,
  nullable(string())   library_key,
  nullable(string())   source_file_key,
  boolean()            is_team_template,
  nullable(timestamp())  last_published_at,
  string()             checkpoint_token,
  string()             realtime_token,
  nullable(string())   file_repo_id,
  nullable(string())   branch_checkpoint_id,
  timestamp()          created_at,
  timestamp()          updated_at,
  timestamp()          touched_at,
  nullable(timestamp())  trashed_at,
  nullable(timestamp())  deleted_at,
);

object(Meta, array(FileJson) files);
object(Envelope, boolean() error, u16() status, (Meta) meta, nullable(string()) i18n);

static FileJson sample(cilo_arena *a, int i) {
  const char *key = aprintf(a, "f%05dAbCdEfGhIjKlMnOp", i);
  const char *slug = "Landing-page-v2";
  const char *base = "https://vektra.example.com";
  return (FileJson){
    .key = key,
    .id = aprintf(a, "%d", 1000000 + i),
    .name = "Landing page v2 \xe2\x80\x94 \"final\"",
    .description = {i % 3 == 0, "Marketing site, desktop and mobile frames"},
    .creator_id = {true, "42"},
    .folder_id = {true, aprintf(a, "%d", 500 + i % 7)},
    .team_id = {true, "7"},
    .editor_type = "design",
    .link_access = "inherit",
    .proto_link_access = "inherit",
    .org_browsable = true,
    .folder_access_enabled = true,
    .url = aprintf(a, "%s/file/%s/%s", base, key, slug),
    .edit_url = aprintf(a, "%s/design/%s/%s", base, key, slug),
    .prototype_url = aprintf(a, "%s/proto/%s/%s", base, key, slug),
    .handoff_url = aprintf(a, "%s/design/%s/%s?m=dev", base, key, slug),
    .slides_url = aprintf(a, "%s/slides/%s/%s", base, key, slug),
    .thumbnail_url = aprintf(a, "%s/thumbnails/%s?v=1793281630000", base, key),
    .thumbnail_guid = {true, "0:1"},
    .client_meta = {true, "{\"background_color\":{\"r\":0.96,\"g\":0.96,\"b\":0.96,\"a\":1}}"},
    .last_published_at = {i % 2 == 0, 1793281630000 + i},
    .checkpoint_token = "c2lnbmVkOmNoZWNrcG9pbnQ6ZjAwMDAxOjEyMzQ1Njc4OTA",
    .realtime_token = "c2lnbmVkOnJlYWx0aW1lOmYwMDAwMTo0Mg.aGVsbG8gd29ybGQ",
    .created_at = 1761745630000 + i * 1000,
    .updated_at = 1793281630123,
    .touched_at = 1793281630456,
  };
}

static double now(void) {
  struct timespec ts;
  timespec_get(&ts, TIME_UTC);
  return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

// the best of nine rounds, in nanoseconds per run of `body`.
#define MEASURE(result, iterations, ...)                                                                        \
  do {                                                                                                           \
    double best_ = 1e300;                                                                                        \
    for (int round_ = 0; round_ < 9; round_++) {                                                                 \
      double start_ = now();                                                                                     \
      for (int it_ = 0; it_ < (iterations); it_++) {                                                             \
        __VA_ARGS__;                                                                                             \
      }                                                                                                          \
      double per_ = (now() - start_) / (iterations);                                                             \
      if (per_ < best_) best_ = per_;                                                                            \
    }                                                                                                            \
    result = best_;                                                                                              \
  } while (0)

static void row(const char *name, double ns, size_t bytes) {
  printf("| %-38s | %10.1f | %8.0f |\n", name, ns / 1000, (double)bytes / ns * 1e3);
}

static volatile size_t sink;

static void run(int files, int iterations) {
  with_arena(data) {
    Envelope env = {.status = 200};
    env.meta.files = (FileJson_list){.items = new_array(data, FileJson, files), .len = (size_t)files};
    for (int i = 0; i < files; i++) env.meta.files.items[i] = sample(data, i);
    size_t len;
    char *text = cima_to_json(&Envelope_schema, &env, &(cima_options){.arena = data}, &len);
    printf("\n%d file%s, %zu bytes. microseconds per run, and MB/s of json.\n\n", files, files == 1 ? "" : "s", len);
    printf("| %-38s | %10s | %8s |\n", "operation", "us", "MB/s");
    printf("| %-38s | %10s | %8s |\n", "--------------------------------------", "----------", "--------");

    cilo_arena work = {};
    double ns;
    MEASURE(ns, iterations, rewinding(&work) {
      Envelope out;
      cima_result r = cima_parse_json(&Envelope_schema, text, len, &out, &(cima_options){.arena = &work});
      sink += r.ok + out.meta.files.len;
    });
    row("typed parse, cima_parse_json", ns, len);
    MEASURE(ns, iterations, rewinding(&work) {
      size_t n;
      cima_to_json(&Envelope_schema, &env, &(cima_options){.arena = &work}, &n);
      sink += n;
    });
    row("typed encode, cima_to_json", ns, len);
    MEASURE(ns, iterations, rewinding(&work) {
      cima_json *doc = json_parse(&work, text, len, nullptr);
      sink += json_len(doc);
    });
    row("dom parse, cima_json_parse", ns, len);
    cima_json *doc = json_parse(data, text, len, nullptr);
    MEASURE(ns, iterations, rewinding(&work) {
      size_t n;
      cima_json_text(CILO_OF(&work), doc, &n);
      sink += n;
    });
    row("dom write, cima_json_text", ns, len);
    MEASURE(ns, iterations, rewinding(&work) {
      size_t n;
      cima_json_text(CILO_OF(&work),
                     json_obj("error", false, "status", 200, "meta", json_typed(Meta, &env.meta), "i18n", nullptr),
                     &n);
      sink += n;
    });
    row("builder envelope around a typed value", ns, len);
    MEASURE(ns, iterations, sink += cima_json_valid(text, len));
    row("validate only, cima_json_valid", ns, len);
    MEASURE(ns, iterations, rewinding(&work) {
      cima_json *copy = json_parse(&work, text, len, nullptr);
      cima_json *files_now = json_at(copy, "/meta/files");
      json_set(&work, json_index(files_now, 0), "name", "renamed");
      sink += json_diff(&work, json_at(doc, "/meta/files/0"), json_index(files_now, 0)).len;
    });
    row("dom parse, edit and diff one object", ns, len);
    cilo_arena_drop(&work);
  }
}

int main(void) {
  run(1, 20000);
  run(100, 300);
  return sink == 0;
}
