#ifndef CIMA_JSON_DSL_H
#define CIMA_JSON_DSL_H

// the short names of the dom, the builder and the writer alone, for code that keeps its own `string` or
// `matches`. cima/dsl.h includes them with the rest.
#include "json.h"

#define json_parse(...)         cima_json_parse(__VA_ARGS__)
#define json_get(...)           cima_json_get(__VA_ARGS__)
#define json_at(...)            cima_json_at(__VA_ARGS__)
#define json_index(...)         cima_json_index(__VA_ARGS__)
#define json_or(...)            CIMA_JSON_OR(__VA_ARGS__)
#define json_get_or(...)        CIMA_JSON_GET_OR(__VA_ARGS__)
#define json_coerce(...)        CIMA_JSON_COERCE(__VA_ARGS__)
#define json_get_coerce(...)    CIMA_JSON_GET_COERCE(__VA_ARGS__)
#define json_type(...)          cima_json_type_of(__VA_ARGS__)
#define json_len(...)           cima_json_len(__VA_ARGS__)
#define json_items(...)         cima_json_array(__VA_ARGS__)
#define json_members(...)       cima_json_object(__VA_ARGS__)
#define json_obj(...)           CIMA_JSON_OBJ(__VA_ARGS__)
#define json_arr(...)           CIMA_JSON_ARR(__VA_ARGS__)
#define json_raw(...)           CIMA_JSON_RAW(__VA_ARGS__)
#define json_typed(...)         CIMA_JSON_TYPED(__VA_ARGS__)
#define json_of(...)            CIMA_JSON_OF(__VA_ARGS__)
#define json_clone(...)         cima_json_clone(__VA_ARGS__)
#define json_set(...)           CIMA_JSON_SET(__VA_ARGS__)
#define json_insert(...)        CIMA_JSON_INSERT(__VA_ARGS__)
#define json_append(...)        CIMA_JSON_APPEND(__VA_ARGS__)
#define json_obj_push(...)      CIMA_JSON_OBJ_PUSH(__VA_ARGS__)
#define json_remove(...)        cima_json_remove(__VA_ARGS__)
#define json_remove_at(...)     cima_json_remove_at(__VA_ARGS__)
#define json_eq(...)            cima_json_eq(__VA_ARGS__)
#define json_diff(...)          cima_json_diff(__VA_ARGS__)
#define json_patch(...)         cima_json_patch(__VA_ARGS__)
#define json_text(a, v)         cima_json_text(CILO_OF(a), v, nullptr)
#define json_pretty(a, v)       cima_json_pretty(CILO_OF(a), v, nullptr)
#define json_writer(...)        CIMA_JSON_WRITER(__VA_ARGS__)
#define json_put(...)           CIMA_JSON_PUT(__VA_ARGS__)
#define json_put_typed(...)     CIMA_JSON_PUT_TYPED(__VA_ARGS__)
#define json_field(...)         CIMA_JSON_FIELD(__VA_ARGS__)
#define json_key(...)           cima_json_write_key(__VA_ARGS__)
#define json_begin_object(...)  cima_json_begin_object(__VA_ARGS__)
#define json_end_object(...)    cima_json_end_object(__VA_ARGS__)
#define json_begin_array(...)   cima_json_begin_array(__VA_ARGS__)
#define json_end_array(...)     cima_json_end_array(__VA_ARGS__)
#define json_finish(w)          cima_json_writer_finish(w, nullptr)
#define json_format(...)        CIMA_JSON_FORMAT(__VA_ARGS__)

#endif
