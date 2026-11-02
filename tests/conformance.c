#include "test.h"

// JSONTestSuite cases, inline. y_ must parse, n_ must not, i_ are up to the parser and cima rejects them.
typedef struct json_case {
  const char *name;
  const char *text;
  size_t len;
} json_case;

#define T(name, text) {name, text, sizeof(text) - 1}

static const json_case accept[] = {
  T("y_array_arraysWithSpaces", "[[]   ]"),
  T("y_array_empty-string", "[\"\"]"),
  T("y_array_empty", "[]"),
  T("y_array_ending_with_newline", "[\"a\"]"),
  T("y_array_false", "[false]"),
  T("y_array_heterogeneous", "[null, 1, \"1\", {}]"),
  T("y_array_null", "[null]"),
  T("y_array_with_1_and_newline", "[1\n]"),
  T("y_array_with_leading_space", " [1]"),
  T("y_array_with_several_null", "[1,null,null,null,2]"),
  T("y_array_with_trailing_space", "[2] "),
  T("y_number", "[123e65]"),
  T("y_number_0e+1", "[0e+1]"),
  T("y_number_0e1", "[0e1]"),
  T("y_number_after_space", "[ 4]"),
  T("y_number_double_close_to_zero", "[-0.000000000000000000000000000000000000000000000000000000000000000000000000000001]\n"),
  T("y_number_int_with_exp", "[20e1]"),
  T("y_number_minus_zero", "[-0]"),
  T("y_number_negative_int", "[-123]"),
  T("y_number_negative_one", "[-1]"),
  T("y_number_negative_zero", "[-0]"),
  T("y_number_real_capital_e", "[1E22]"),
  T("y_number_real_capital_e_neg_exp", "[1E-2]"),
  T("y_number_real_capital_e_pos_exp", "[1E+2]"),
  T("y_number_real_exponent", "[123e45]"),
  T("y_number_real_fraction_exponent", "[123.456e78]"),
  T("y_number_real_neg_exp", "[1e-2]"),
  T("y_number_real_pos_exponent", "[1e+2]"),
  T("y_number_simple_int", "[123]"),
  T("y_number_simple_real", "[123.456789]"),
  T("y_object", "{\"asd\":\"sdf\", \"dfg\":\"fgh\"}"),
  T("y_object_basic", "{\"asd\":\"sdf\"}"),
  T("y_object_duplicated_key", "{\"a\":\"b\",\"a\":\"c\"}"),
  T("y_object_duplicated_key_and_value", "{\"a\":\"b\",\"a\":\"b\"}"),
  T("y_object_empty", "{}"),
  T("y_object_empty_key", "{\"\":0}"),
  T("y_object_escaped_null_in_key", "{\"foo\\u0000bar\": 42}"),
  T("y_object_extreme_numbers", "{ \"min\": -1.0e+28, \"max\": 1.0e+28 }"),
  T("y_object_long_strings", "{\"x\":[{\"id\": \"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\"}], \"id\": "
                             "\"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\"}"),
  T("y_object_simple", "{\"a\":[]}"),
  T("y_object_string_unicode", "{\"title\":\"\\u041f\\u043e\\u043b\\u0442\\u043e\\u0440\\u0430 "
                               "\\u0417\\u0435\\u043c\\u043b\\u0435\\u043a\\u043e\\u043f\\u0430\" }"),
  T("y_object_with_newlines", "{\n\"a\": \"b\"\n}"),
  T("y_string_1_2_3_bytes_UTF-8_sequences", "[\"\\u0060\\u012a\\u12AB\"]"),
  T("y_string_accepted_surrogate_pair", "[\"\\uD801\\udc37\"]"),
  T("y_string_accepted_surrogate_pairs", "[\"\\ud83d\\ude39\\ud83d\\udc8d\"]"),
  T("y_string_allowed_escapes", "[\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"]"),
  T("y_string_backslash_and_u_escaped_zero", "[\"\\\\u0000\"]"),
  T("y_string_backslash_doublequotes", "[\"\\\"\"]"),
  T("y_string_comments", "[\"a/*b*/c/*d//e\"]"),
  T("y_string_double_escape_a", "[\"\\\\a\"]"),
  T("y_string_double_escape_n", "[\"\\\\n\"]"),
  T("y_string_escaped_control_character", "[\"\\u0012\"]"),
  T("y_string_escaped_noncharacter", "[\"\\uFFFF\"]"),
  T("y_string_in_array", "[\"asd\"]"),
  T("y_string_in_array_with_leading_space", "[ \"asd\"]"),
  T("y_string_last_surrogates_1_and_2", "[\"\\uDBFF\\uDFFF\"]"),
  T("y_string_nbsp_uescaped", "[\"new\\u00A0line\"]"),
  T("y_string_nonCharacterInUTF-8_U+10FFFF", "[\"\xf4\x8f\xbf\xbf\"]"),
  T("y_string_nonCharacterInUTF-8_U+FFFF", "[\"\xef\xbf\xbf\"]"),
  T("y_string_null_escape", "[\"\\u0000\"]"),
  T("y_string_one-byte-utf-8", "[\"\\u002c\"]"),
  T("y_string_pi", "[\"\xcf\x80\"]"),
  T("y_string_reservedCharacterInUTF-8_U+1BFFF", "[\"\xf0\x9b\xbf\xbf\"]"),
  T("y_string_simple_ascii", "[\"asd \"]"),
  T("y_string_space", "\" \""),
  T("y_string_surrogates_U+1D11E_MUSICAL_SYMBOL_G_CLEF", "[\"\\uD834\\uDd1e\"]"),
  T("y_string_three-byte-utf-8", "[\"\\u0821\"]"),
  T("y_string_two-byte-utf-8", "[\"\\u0123\"]"),
  T("y_string_u+2028_line_sep", "[\"\xe2\x80\xa8\"]"),
  T("y_string_u+2029_par_sep", "[\"\xe2\x80\xa9\"]"),
  T("y_string_uEscape", "[\"\\u0061\\u30af\\u30EA\\u30b9\"]"),
  T("y_string_uescaped_newline", "[\"new\\u000Aline\"]"),
  T("y_string_unescaped_char_delete", "[\"\x7f\"]"),
  T("y_string_unicode", "[\"\\uA66D\"]"),
  T("y_string_unicodeEscapedBackslash", "[\"\\u005C\"]"),
  T("y_string_unicode_2", "[\"\xe2\x8d\x82\xe3\x88\xb4\xe2\x8d\x82\"]"),
  T("y_string_unicode_U+10FFFE_nonchar", "[\"\\uDBFF\\uDFFE\"]"),
  T("y_string_unicode_U+1FFFE_nonchar", "[\"\\uD83F\\uDFFE\"]"),
  T("y_string_unicode_U+200B_ZERO_WIDTH_SPACE", "[\"\\u200B\"]"),
  T("y_string_unicode_U+2064_invisible_plus", "[\"\\u2064\"]"),
  T("y_string_unicode_U+FDD0_nonchar", "[\"\\uFDD0\"]"),
  T("y_string_unicode_U+FFFE_nonchar", "[\"\\uFFFE\"]"),
  T("y_string_unicode_escaped_double_quote", "[\"\\u0022\"]"),
  T("y_string_utf8", "[\"\xe2\x82\xac\xf0\x9d\x84\x9e\"]"),
  T("y_string_with_del_character", "[\"a\x7f" "a\"]"),
  T("y_structure_lonely_false", "false"),
  T("y_structure_lonely_int", "42"),
  T("y_structure_lonely_negative_real", "-0.1"),
  T("y_structure_lonely_null", "null"),
  T("y_structure_lonely_string", "\"asd\""),
  T("y_structure_lonely_true", "true"),
  T("y_structure_string_empty", "\"\""),
  T("y_structure_trailing_newline", "[\"a\"]\n"),
  T("y_structure_true_in_array", "[true]"),
  T("y_structure_whitespace_array", " [] "),
  T("y_number_big_ints", "[9223372036854775807, -9223372036854775808, 18446744073709551615]"),
  T("y_number_past_u64", "[18446744073709551616, -9223372036854775809]"),
  T("y_number_underflow", "[123e-10000000]"),
  T("y_number_long_mantissa", "[0.12345678901234567890123456789012345678901234567890]"),
};

static const json_case reject[] = {
  T("n_array_1_true_without_comma", "[1 true]"),
  T("n_array_a_invalid_utf8", "[a\xe5]"),
  T("n_array_colon_instead_of_comma", "[\"\": 1]"),
  T("n_array_comma_after_close", "[\"\"],"),
  T("n_array_comma_and_number", "[,1]"),
  T("n_array_double_comma", "[1,,2]"),
  T("n_array_double_extra_comma", "[\"x\",,]"),
  T("n_array_extra_close", "[\"x\"]]"),
  T("n_array_extra_comma", "[\"\",]"),
  T("n_array_incomplete", "[\"x\""),
  T("n_array_incomplete_invalid_value", "[x"),
  T("n_array_inner_array_no_comma", "[3[4]]"),
  T("n_array_invalid_utf8", "[\xff]"),
  T("n_array_items_separated_by_semicolon", "[1:2]"),
  T("n_array_just_comma", "[,]"),
  T("n_array_just_minus", "[-]"),
  T("n_array_missing_value", "[   , \"\"]"),
  T("n_array_newlines_unclosed", "[\"a\",\n4\n,1,"),
  T("n_array_number_and_comma", "[1,]"),
  T("n_array_number_and_several_commas", "[1,,]"),
  T("n_array_spaces_vertical_tab_formfeed", "[\"\va\"\\f]"),
  T("n_array_star_inside", "[*]"),
  T("n_array_unclosed", "[\"\""),
  T("n_array_unclosed_trailing_comma", "[1,"),
  T("n_array_unclosed_with_new_lines", "[1,\n1\n,1"),
  T("n_array_unclosed_with_object_inside", "[{}"),
  T("n_incomplete_false", "[fals]"),
  T("n_incomplete_null", "[nul]"),
  T("n_incomplete_true", "[tru]"),
  T("n_multidigit_number_then_00", "123\0"),
  T("n_number_++", "[++1234]"),
  T("n_number_+1", "[+1]"),
  T("n_number_+Inf", "[+Inf]"),
  T("n_number_-01", "[-01]"),
  T("n_number_-1.0.", "[-1.0.]"),
  T("n_number_-2.", "[-2.]"),
  T("n_number_-NaN", "[-NaN]"),
  T("n_number_.-1", "[.-1]"),
  T("n_number_.2e-3", "[.2e-3]"),
  T("n_number_0.1.2", "[0.1.2]"),
  T("n_number_0.3e+", "[0.3e+]"),
  T("n_number_0.3e", "[0.3e]"),
  T("n_number_0.e1", "[0.e1]"),
  T("n_number_0_capital_E+", "[0E+]"),
  T("n_number_0_capital_E", "[0E]"),
  T("n_number_0e+", "[0e+]"),
  T("n_number_0e", "[0e]"),
  T("n_number_1.0e+", "[1.0e+]"),
  T("n_number_1.0e-", "[1.0e-]"),
  T("n_number_1.0e", "[1.0e]"),
  T("n_number_1_000", "[1 000.0]"),
  T("n_number_1eE2", "[1eE2]"),
  T("n_number_2.e+3", "[2.e+3]"),
  T("n_number_2.e-3", "[2.e-3]"),
  T("n_number_2.e3", "[2.e3]"),
  T("n_number_9.e+", "[9.e+]"),
  T("n_number_Inf", "[Inf]"),
  T("n_number_NaN", "[NaN]"),
  T("n_number_U+FF11_fullwidth_digit_one", "[\xef\xbc\x91]"),
  T("n_number_expression", "[1+2]"),
  T("n_number_hex_1_digit", "[0x1]"),
  T("n_number_hex_2_digits", "[0x42]"),
  T("n_number_infinity", "[Infinity]"),
  T("n_number_invalid+-", "[0e+-1]"),
  T("n_number_invalid-negative-real", "[-123.123foo]"),
  T("n_number_invalid-utf-8-in-bigger-int", "[123\xe5]"),
  T("n_number_invalid-utf-8-in-exponent", "[1e1\xe5]"),
  T("n_number_invalid-utf-8-in-int", "[0\xe5]\n"),
  T("n_number_minus_infinity", "[-Infinity]"),
  T("n_number_minus_sign_with_trailing_garbage", "[-foo]"),
  T("n_number_minus_space_1", "[- 1]"),
  T("n_number_neg_int_starting_with_zero", "[-012]"),
  T("n_number_neg_real_without_int_part", "[-.123]"),
  T("n_number_neg_with_garbage_at_end", "[-1x]"),
  T("n_number_real_garbage_after_e", "[1ea]"),
  T("n_number_real_with_invalid_utf8_after_e", "[1e\xe5]"),
  T("n_number_real_without_fractional_part", "[1.]"),
  T("n_number_starting_with_dot", "[.123]"),
  T("n_number_with_alpha", "[1.2a-3]"),
  T("n_number_with_alpha_char", "[1.8011670033376514H-308]"),
  T("n_number_with_leading_zero", "[012]"),
  T("n_object_bad_value", "[\"x\", truth]"),
  T("n_object_bracket_key", "{[: \"x\"}\n"),
  T("n_object_comma_instead_of_colon", "{\"x\", null}"),
  T("n_object_double_colon", "{\"x\"::\"b\"}"),
  T("n_object_emoji", "{\xf0\x9f\x87\xa8\xf0\x9f\x87\xad}"),
  T("n_object_garbage_at_end", "{\"a\":\"a\" 123}"),
  T("n_object_key_with_single_quotes", "{key: 'value'}"),
  T("n_object_lone_continuation_byte_in_key_and_trailing_comma", "{\"\xb9\":\"0\",}"),
  T("n_object_missing_colon", "{\"a\" b}"),
  T("n_object_missing_key", "{:\"b\"}"),
  T("n_object_missing_semicolon", "{\"a\" \"b\"}"),
  T("n_object_missing_value", "{\"a\":"),
  T("n_object_no-colon", "{\"a\""),
  T("n_object_non_string_key", "{1:1}"),
  T("n_object_non_string_key_but_huge_number_instead", "{9999E9999:1}"),
  T("n_object_repeated_null_null", "{null:null,null:null}"),
  T("n_object_several_trailing_commas", "{\"id\":0,,,,,}"),
  T("n_object_single_quote", "{'a':0}"),
  T("n_object_trailing_comma", "{\"id\":0,}"),
  T("n_object_trailing_comment", "{\"a\":\"b\"}/**/"),
  T("n_object_trailing_comment_open", "{\"a\":\"b\"}/**//"),
  T("n_object_trailing_comment_slash_open", "{\"a\":\"b\"}//"),
  T("n_object_trailing_comment_slash_open_incomplete", "{\"a\":\"b\"}/"),
  T("n_object_two_commas_in_a_row", "{\"a\":\"b\",,\"c\":\"d\"}"),
  T("n_object_unquoted_key", "{a: \"b\"}"),
  T("n_object_unterminated-value", "{\"a\":\"a"),
  T("n_object_with_single_string", "{ \"foo\" : \"bar\", \"a\" }"),
  T("n_object_with_trailing_garbage", "{\"a\":\"b\"}#"),
  T("n_single_space", " "),
  T("n_string_1_surrogate_then_escape", "[\"\\uD800\\\"]"),
  T("n_string_1_surrogate_then_escape_u", "[\"\\uD800\\u\"]"),
  T("n_string_1_surrogate_then_escape_u1", "[\"\\uD800\\u1\"]"),
  T("n_string_1_surrogate_then_escape_u1x", "[\"\\uD800\\u1x\"]"),
  T("n_string_accentuated_char_no_quotes", "[\xc3\xa9]"),
  T("n_string_backslash_00", "[\"\\\0\"]"),
  T("n_string_escape_x", "[\"\\x00\"]"),
  T("n_string_escaped_backslash_bad", "[\"\\\\\\\"]"),
  T("n_string_escaped_ctrl_char_tab", "[\"\\\t\"]"),
  T("n_string_escaped_emoji", "[\"\\\xf0\x9f\x8c\x80\"]"),
  T("n_string_incomplete_escape", "[\"\\\"]"),
  T("n_string_incomplete_escaped_character", "[\"\\u00A\"]"),
  T("n_string_incomplete_surrogate", "[\"\\uD834\\uDd\"]"),
  T("n_string_incomplete_surrogate_escape_invalid", "[\"\\uD800\\uD800\\x\"]"),
  T("n_string_invalid-utf-8-in-escape", "[\"\\u\xe5\"]"),
  T("n_string_invalid_backslash_esc", "[\"\\a\"]"),
  T("n_string_invalid_unicode_escape", "[\"\\uqqqq\"]"),
  T("n_string_invalid_utf8_after_escape", "[\"\\\xe5\"]"),
  T("n_string_leading_uescaped_thinspace", "[\\u0020\"asd\"]"),
  T("n_string_no_quotes_with_bad_escape", "[\\n]"),
  T("n_string_single_doublequote", "\""),
  T("n_string_single_quote", "['single quote']"),
  T("n_string_single_string_no_double_quotes", "abc"),
  T("n_string_start_escape_unclosed", "[\"\\"),
  T("n_string_unescaped_ctrl_char", "[\"a\0a\"]"),
  T("n_string_unescaped_newline", "[\"new\nline\"]"),
  T("n_string_unescaped_tab", "[\"\t\"]"),
  T("n_string_unicode_CapitalU", "\"\\UA66D\""),
  T("n_string_with_trailing_garbage", "\"\"x"),
  T("n_structure_U+2060_word_joined", "[\xe2\x81\xa0]"),
  T("n_structure_UTF8_BOM_no_data", "\xef\xbb\xbf"),
  T("n_structure_angle_bracket_.", "<.>"),
  T("n_structure_angle_bracket_null", "[<null>]"),
  T("n_structure_array_trailing_garbage", "[1]x"),
  T("n_structure_array_with_extra_array_close", "[1]]"),
  T("n_structure_array_with_unclosed_string", "[\"asd]"),
  T("n_structure_ascii-unicode-identifier", "a\xc3\xa5"),
  T("n_structure_capitalized_True", "[True]"),
  T("n_structure_close_unopened_array", "1]"),
  T("n_structure_comma_instead_of_closing_brace", "{\"x\": true,"),
  T("n_structure_double_array", "[][]"),
  T("n_structure_end_array", "]"),
  T("n_structure_lone-open-bracket", "["),
  T("n_structure_no_data", ""),
  T("n_structure_null-byte-outside-string", "[\0]"),
  T("n_structure_number_with_trailing_garbage", "2@"),
  T("n_structure_object_followed_by_closing_object", "{}}"),
  T("n_structure_object_unclosed_no_value", "{\"\":"),
  T("n_structure_object_with_comment", "{\"a\":/*comment*/\"b\"}"),
  T("n_structure_object_with_trailing_garbage", "{\"a\": true} \"x\""),
  T("n_structure_open_array_apostrophe", "['"),
  T("n_structure_open_array_comma", "[,"),
  T("n_structure_open_array_open_object", "[{"),
  T("n_structure_open_array_open_string", "[\"a"),
  T("n_structure_open_array_string", "[\"a\""),
  T("n_structure_open_object", "{"),
  T("n_structure_open_object_close_array", "{]"),
  T("n_structure_open_object_comma", "{,"),
  T("n_structure_open_object_open_array", "{["),
  T("n_structure_open_object_open_string", "{\"a"),
  T("n_structure_open_object_string_with_apostrophes", "{'a'"),
  T("n_structure_single_star", "*"),
  T("n_structure_trailing_#", "{\"a\":\"b\"}#{}"),
  T("n_structure_uescaped_LF_before_string", "[\\u000A\"\"]"),
  T("n_structure_unclosed_array", "[1"),
  T("n_structure_unclosed_array_partial_null", "[ false, nul"),
  T("n_structure_unclosed_array_unfinished_false", "[ true, fals"),
  T("n_structure_unclosed_array_unfinished_true", "[ false, tru"),
  T("n_structure_unclosed_object", "{\"asd\":\"asd\""),
  T("n_structure_unicode-identifier", "\xc3\xa5"),
  T("n_structure_whitespace_U+2060_word_joiner", "[\xe2\x81\xa0]"),
  T("n_structure_whitespace_formfeed", "[\f]"),
  T("i_object_key_lone_2nd_surrogate", "{\"\\uDFAA\":0}"),
  T("i_string_1st_surrogate_but_2nd_missing", "[\"\\uDADA\"]"),
  T("i_string_1st_valid_surrogate_2nd_invalid", "[\"\\uD888\\u1234\"]"),
  T("i_string_UTF-16LE_with_BOM", "\xff\xfe[\0\"\0\xe9\0\"\0]\0"),
  T("i_string_UTF-8_invalid_sequence", "[\"\xe6\x97\xa5\xd1\x88\xfa\"]"),
  T("i_string_UTF8_surrogate_U+D800", "[\"\xed\xa0\x80\"]"),
  T("i_string_incomplete_surrogate_and_escape_valid", "[\"\\uD800\\n\"]"),
  T("i_string_incomplete_surrogate_pair", "[\"\\uDd1ea\"]"),
  T("i_string_incomplete_surrogates_escape_valid", "[\"\\uD800\\uD800\\n\"]"),
  T("i_string_invalid_lonely_surrogate", "[\"\\ud800\"]"),
  T("i_string_invalid_surrogate", "[\"\\ud800abc\"]"),
  T("i_string_invalid_utf-8", "[\"\xff\"]"),
  T("i_string_inverted_surrogates_U+1D11E", "[\"\\uDd1e\\uD834\"]"),
  T("i_string_iso_latin_1", "[\"\xe9\"]"),
  T("i_string_lone_second_surrogate", "[\"\\uDFAA\"]"),
  T("i_string_lone_utf8_continuation_byte", "[\"\x81\"]"),
  T("i_string_not_in_unicode_range", "[\"\xf4\xbf\xbf\xbf\"]"),
  T("i_string_overlong_sequence_2_bytes", "[\"\xc0\xaf\"]"),
  T("i_string_overlong_sequence_6_bytes", "[\"\xfc\x83\xbf\xbf\xbf\xbf\"]"),
  T("i_string_overlong_sequence_6_bytes_null", "[\"\xfc\x80\x80\x80\x80\x80\"]"),
  T("i_string_truncated-utf-8", "[\"\xe0\xff\"]"),
  T("i_string_utf16BE_no_BOM", "\0[\0\"\0\xe9\0\"\0]"),
  T("i_structure_UTF-8_BOM_empty_object", "\xef\xbb\xbf{}"),
};

// valid grammar, past the range of a double: kept as text by unknown(), refused by the dom and number().
static const json_case overflow[] = {
  T("i_number_huge_exp", "[0.4e00669999999999999999999999999999999999999999999999999999999999999999999999999]"),
  T("i_number_neg_int_huge_exp", "[-1e+9999]"),
  T("i_number_pos_double_huge_exp", "[1.5e+9999]"),
  T("i_number_real_neg_overflow", "[-123123e100000]"),
  T("i_number_real_pos_overflow", "[123123e100000]"),
};

static void suite(void) {
  for (size_t i = 0; i < sizeof accept / sizeof *accept; i++) {
    const json_case *t = &accept[i];
    with_arena(a) {
      cima_json_error err = {};
      cima_json *dom = json_parse(a, t->text, t->len, &err);
      if (!dom) fprintf(stderr, "%s: %s at %zu\n", t->name, err.message, err.offset);
      CHECK(dom != nullptr);
      CHECK(cima_json_valid(t->text, t->len));
      cima_unknown raw = nullptr;
      CHECK(cima_parse_json(&cima_unknown_schema, t->text, t->len, &raw, &(cima_options){.arena = a}).ok);
      // what the dom writes reads back the same.
      if (dom) {
        size_t len;
        char *text = cima_json_text(CILO_OF(a), dom, &len);
        cima_json *again = json_parse(a, text, len, nullptr);
        if (!again || !json_eq(dom, again)) fprintf(stderr, "%s: round trip gave %s\n", t->name, text);
        CHECK(again && json_eq(dom, again));
      }
    }
  }
  for (size_t i = 0; i < sizeof reject / sizeof *reject; i++) {
    const json_case *t = &reject[i];
    with_arena(a) {
      cima_json_error err = {};
      cima_json *dom = json_parse(a, t->text, t->len, &err);
      if (dom) fprintf(stderr, "%s: accepted\n", t->name);
      CHECK(dom == nullptr && err.message != nullptr && err.offset <= t->len);
      if (cima_json_valid(t->text, t->len)) fprintf(stderr, "%s: valid\n", t->name);
      CHECK(!cima_json_valid(t->text, t->len));
      cima_unknown raw = nullptr;
      cima_result r = cima_parse_json(&cima_unknown_schema, t->text, t->len, &raw, &(cima_options){.arena = a});
      CHECK(!r.ok && r.issues.len == 1);
      CHECK(r.ok || r.issues.items[0].code == CIMA_ISSUE_INVALID_JSON);
      CHECK(raw == nullptr);
    }
  }
}

static void overflowing(void) {
  for (size_t i = 0; i < sizeof overflow / sizeof *overflow; i++) {
    const json_case *t = &overflow[i];
    with_arena(a) {
      CHECK(cima_json_valid(t->text, t->len));
      cima_unknown raw = nullptr;
      CHECK(cima_parse_json(&cima_unknown_schema, t->text, t->len, &raw, &(cima_options){.arena = a}).ok);
      cima_json_error err;
      CHECK(!json_parse(a, t->text, t->len, &err) && strcmp(err.message, "number out of range") == 0);
      typeof(CIMA_CTYPE(array(number()))) numbers;
      CHECK(!CIMA_PARSE(array(number()), t->text, &numbers, .arena = a).ok);
    }
  }
}

static void nesting(void) {
  with_arena(a) {
    char deep[1200];
    for (int i = 0; i < 600; i++) deep[i] = '[', deep[1199 - i] = ']';
    // the dom stops at 512 levels, the typed parser at max_depth.
    CHECK(!json_parse(a, deep, sizeof deep, nullptr));
    CHECK(json_parse(a, deep + 100, sizeof deep - 200, nullptr));
    cima_unknown raw;
    cima_result r = cima_parse_json(&cima_unknown_schema, deep + 100, sizeof deep - 200, &raw,
                                    &(cima_options){.arena = a, .max_depth = 64});
    CHECK(!r.ok);
    r = cima_parse_json(&cima_unknown_schema, deep + 100, sizeof deep - 200, &raw,
                        &(cima_options){.arena = a, .max_depth = 600});
    CHECK(r.ok);
    // far past any limit, no stack overflow.
    size_t n = 1 << 20;
    char *opening = new_array(a, char, n);
    memset(opening, '[', n);
    CHECK(!json_parse(a, opening, n, nullptr));
    CHECK(!cima_json_valid(opening, n));
  }
}

static void decoding(void) {
  with_arena(a) {
    static const char text[] = "[\"\\ud83d\\ude39\\u00e9\\n\\u0000x\", \"\xe2\x82\xac\"]";
    cima_json *v = json_parse(a, text, sizeof text - 1, nullptr);
    CHECK(v != nullptr);
    cima_json *s = json_index(v, 0);
    CHECK(s->len == 9 && memcmp(s->str, "\xf0\x9f\x98\xb9\xc3\xa9\n\0x", 10) == 0);
    CHECK_STR(json_text(a, v), "[\"\xf0\x9f\x98\xb9\xc3\xa9\\n\\u0000x\",\"\xe2\x82\xac\"]");
    cima_json_error err;
    CHECK(!json_parse(a, "[1, 2", 5, &err) && err.offset == 5);
    CHECK(!json_parse(a, "{\"a\" 1}", 7, &err) && err.offset == 5 && strcmp(err.message, "expected ':'") == 0);
  }
}

int main(void) {
  suite();
  overflowing();
  nesting();
  decoding();
  return report_failures();
}
