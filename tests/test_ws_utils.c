/*
 * Unit tests for libwildlifesystems ws_utils
 * Tests JSON parsing, config handling, and utility functions
 * without requiring actual hardware.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <math.h>

#include "unity.h"
#include "../src/ws_utils.h"

/* ========== Test Fixtures ========== */

static char temp_file_path[256];
static int temp_file_counter = 0;

void setUp(void) {
    snprintf(temp_file_path, sizeof(temp_file_path), 
             "/tmp/ws_utils_test_%d_%d.json", getpid(), temp_file_counter++);
}

void tearDown(void) {
    unlink(temp_file_path);
}

/* Helper to write temp file */
static void write_file(const char *content) {
    FILE *fp = fopen(temp_file_path, "w");
    if (fp) {
        fputs(content, fp);
        fclose(fp);
    }
}

/* ========== JSON Escape Tests ========== */

void test_json_escape_simple_string(void) {
    char dst[64];
    ws_json_escape_string("hello", dst, sizeof(dst));
    TEST_ASSERT_EQUAL_STRING("hello", dst);
}

void test_json_escape_with_quotes(void) {
    char dst[64];
    ws_json_escape_string("say \"hello\"", dst, sizeof(dst));
    TEST_ASSERT_EQUAL_STRING("say \\\"hello\\\"", dst);
}

void test_json_escape_with_backslash(void) {
    char dst[64];
    ws_json_escape_string("path\\to\\file", dst, sizeof(dst));
    TEST_ASSERT_EQUAL_STRING("path\\\\to\\\\file", dst);
}

void test_json_escape_null_input(void) {
    char dst[64] = "unchanged";
    ws_json_escape_string(NULL, dst, sizeof(dst));
    TEST_ASSERT_EQUAL_STRING("", dst);
}

/* ========== JSON Object Counting Tests ========== */

void test_json_count_objects_empty(void) {
    TEST_ASSERT_EQUAL_INT(0, ws_json_count_objects("[]"));
}

void test_json_count_objects_single(void) {
    TEST_ASSERT_EQUAL_INT(1, ws_json_count_objects("[{\"a\":1}]"));
}

void test_json_count_objects_multiple(void) {
    TEST_ASSERT_EQUAL_INT(3, ws_json_count_objects("[{\"a\":1},{\"b\":2},{\"c\":3}]"));
}

void test_json_count_objects_null(void) {
    TEST_ASSERT_EQUAL_INT(0, ws_json_count_objects(NULL));
}

/* A brace inside a string value is not the start of an object. */
void test_json_count_objects_brace_in_string(void) {
    TEST_ASSERT_EQUAL_INT(1,
        ws_json_count_objects("[{\"sensor_name\":\"Pond {north bank}\"}]"));
}

/* Token syntax such as "{{node}}" must not inflate the count. */
void test_json_count_objects_token_in_string(void) {
    TEST_ASSERT_EQUAL_INT(2,
        ws_json_count_objects("[{\"location\":\"{{node}}\"},"
                              "{\"location\":\"{{none}}\"}]"));
}

/* Nested objects belong to their parent, not counted separately. */
void test_json_count_objects_nested(void) {
    TEST_ASSERT_EQUAL_INT(1,
        ws_json_count_objects("[{\"location\":{\"latitude\":51.5}}]"));
}

void test_json_count_objects_nested_multiple(void) {
    TEST_ASSERT_EQUAL_INT(2,
        ws_json_count_objects("[{\"location\":{\"latitude\":51.5,"
                              "\"longitude\":-0.17}},"
                              "{\"location\":\"{{node}}\"}]"));
}

/* An escaped quote does not end the string, so the brace stays hidden. */
void test_json_count_objects_escaped_quote(void) {
    TEST_ASSERT_EQUAL_INT(1,
        ws_json_count_objects("[{\"sensor_name\":\"say \\\"{\\\" twice\"}]"));
}

void test_json_count_objects_unterminated(void) {
    /* Still counts the object that was opened; the caller detects the
       truncation via ws_json_object_end() returning NULL. */
    TEST_ASSERT_EQUAL_INT(1, ws_json_count_objects("[{\"a\":1"));
}

/* ========== JSON Object End Tests ========== */

void test_json_object_end_simple(void) {
    const char *json = "{\"a\":1}";
    TEST_ASSERT_TRUE(ws_json_object_end(json) == json + 6);
}

void test_json_object_end_nested(void) {
    const char *json = "{\"loc\":{\"lat\":51.5}}";
    /* Must be the outer brace, not the inner one. */
    TEST_ASSERT_TRUE(ws_json_object_end(json) == json + strlen(json) - 1);
}

void test_json_object_end_brace_in_string(void) {
    const char *json = "{\"name\":\"a } brace\",\"pin\":4}";
    TEST_ASSERT_TRUE(ws_json_object_end(json) == json + strlen(json) - 1);
}

void test_json_object_end_token_in_string(void) {
    const char *json = "{\"location\":\"{{node}}\",\"pin\":4}";
    TEST_ASSERT_TRUE(ws_json_object_end(json) == json + strlen(json) - 1);
}

void test_json_object_end_escaped_quote(void) {
    const char *json = "{\"name\":\"quote \\\" then } brace\",\"pin\":4}";
    TEST_ASSERT_TRUE(ws_json_object_end(json) == json + strlen(json) - 1);
}

void test_json_object_end_leading_whitespace(void) {
    const char *json = "  \n\t{\"a\":1}";
    TEST_ASSERT_TRUE(ws_json_object_end(json) == json + strlen(json) - 1);
}

void test_json_object_end_unterminated(void) {
    TEST_ASSERT_NULL(ws_json_object_end("{\"a\":1"));
}

void test_json_object_end_unterminated_string(void) {
    TEST_ASSERT_NULL(ws_json_object_end("{\"a\":\"no close"));
}

void test_json_object_end_not_an_object(void) {
    TEST_ASSERT_NULL(ws_json_object_end("[{\"a\":1}]"));
}

void test_json_object_end_null(void) {
    TEST_ASSERT_NULL(ws_json_object_end(NULL));
}

/* Walking an array of objects the way the drivers do. */
void test_json_object_end_walks_array(void) {
    const char *json = "[{\"loc\":{\"lat\":1}},{\"loc\":\"{{node}}\"}]";
    const char *first = strchr(json, '{');
    const char *first_end = ws_json_object_end(first);
    TEST_ASSERT_NOT_NULL(first_end);

    const char *second = strchr(first_end + 1, '{');
    const char *second_end = ws_json_object_end(second);
    TEST_ASSERT_NOT_NULL(second_end);

    /* The second object is the last thing before the closing bracket. */
    TEST_ASSERT_EQUAL_INT(']', *(second_end + 1));
}

/* ========== JSON Parse Object Tests ========== */

void test_json_parse_object_found(void) {
    const char *json = "{\"location\":{\"latitude\":51.5},\"pin\":4}";
    const char *end = ws_json_object_end(json);
    char *obj = ws_json_parse_object(json, end, "location");
    TEST_ASSERT_NOT_NULL(obj);
    TEST_ASSERT_EQUAL_STRING("{\"latitude\":51.5}", obj);
    free(obj);
}

void test_json_parse_object_nested_deeper(void) {
    const char *json = "{\"a\":{\"b\":{\"c\":1}},\"pin\":4}";
    const char *end = ws_json_object_end(json);
    char *obj = ws_json_parse_object(json, end, "a");
    TEST_ASSERT_NOT_NULL(obj);
    TEST_ASSERT_EQUAL_STRING("{\"b\":{\"c\":1}}", obj);
    free(obj);
}

void test_json_parse_object_not_found(void) {
    const char *json = "{\"pin\":4}";
    const char *end = ws_json_object_end(json);
    TEST_ASSERT_NULL(ws_json_parse_object(json, end, "location"));
}

/* "location":"{{node}}" is a string, not an object. */
void test_json_parse_object_is_string(void) {
    const char *json = "{\"location\":\"{{node}}\"}";
    const char *end = ws_json_object_end(json);
    TEST_ASSERT_NULL(ws_json_parse_object(json, end, "location"));
}

void test_json_parse_object_null(void) {
    TEST_ASSERT_NULL(ws_json_parse_object(NULL, NULL, "location"));
}

/* ========== JSON Parse String Tests ========== */

void test_json_parse_string_found(void) {
    const char *json = "{\"name\":\"test_sensor\",\"type\":\"dht11\"}";
    const char *end = json + strlen(json);
    char *result = ws_json_parse_string(json, end, "name");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("test_sensor", result);
    free(result);
}

void test_json_parse_string_second_field(void) {
    const char *json = "{\"name\":\"test_sensor\",\"type\":\"dht11\"}";
    const char *end = json + strlen(json);
    char *result = ws_json_parse_string(json, end, "type");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("dht11", result);
    free(result);
}

/* An object-valued field is not a string: must not return the nested key. */
void test_json_parse_string_rejects_object(void) {
    const char *json = "{\"location\":{\"latitude\":51.5},\"pin\":4}";
    const char *end = ws_json_object_end(json);
    TEST_ASSERT_NULL(ws_json_parse_string(json, end, "location"));
}

void test_json_parse_string_rejects_number(void) {
    const char *json = "{\"pin\":4,\"name\":\"sensor\"}";
    const char *end = ws_json_object_end(json);
    TEST_ASSERT_NULL(ws_json_parse_string(json, end, "pin"));
}

void test_json_parse_string_rejects_bool(void) {
    const char *json = "{\"internal\":true,\"name\":\"sensor\"}";
    const char *end = ws_json_object_end(json);
    TEST_ASSERT_NULL(ws_json_parse_string(json, end, "internal"));
}

void test_json_parse_string_rejects_array(void) {
    const char *json = "{\"tags\":[\"a\",\"b\"],\"name\":\"sensor\"}";
    const char *end = ws_json_object_end(json);
    TEST_ASSERT_NULL(ws_json_parse_string(json, end, "tags"));
}

/* The token form of a polymorphic field still parses. */
void test_json_parse_string_token_value(void) {
    const char *json = "{\"location\":\"{{node}}\"}";
    const char *end = ws_json_object_end(json);
    char *result = ws_json_parse_string(json, end, "location");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("{{node}}", result);
    free(result);
}

void test_json_parse_string_whitespace_before_value(void) {
    const char *json = "{\"name\":   \"spaced\"}";
    const char *end = ws_json_object_end(json);
    char *result = ws_json_parse_string(json, end, "name");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("spaced", result);
    free(result);
}

void test_json_parse_string_empty_value(void) {
    const char *json = "{\"name\":\"\"}";
    const char *end = ws_json_object_end(json);
    char *result = ws_json_parse_string(json, end, "name");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("", result);
    free(result);
}

/* An escaped quote must not truncate the value. */
void test_json_parse_string_escaped_quote(void) {
    const char *json = "{\"name\":\"say \\\"hi\\\" now\",\"pin\":4}";
    const char *end = ws_json_object_end(json);
    char *result = ws_json_parse_string(json, end, "name");
    TEST_ASSERT_NOT_NULL(result);
    /* Returned still escaped, exactly as it appears in the JSON. */
    TEST_ASSERT_EQUAL_STRING("say \\\"hi\\\" now", result);
    free(result);
}

void test_json_parse_string_unterminated_value(void) {
    const char *json = "{\"name\":\"no close}";
    const char *end = json + strlen(json);
    TEST_ASSERT_NULL(ws_json_parse_string(json, end, "name"));
}

void test_json_parse_string_not_found(void) {
    const char *json = "{\"name\":\"test_sensor\"}";
    const char *end = json + strlen(json);
    char *result = ws_json_parse_string(json, end, "missing");
    TEST_ASSERT_NULL(result);
}

/* ========== JSON Parse Bool Tests ========== */

void test_json_parse_bool_true(void) {
    const char *json = "{\"internal\":true}";
    const char *end = json + strlen(json);
    TEST_ASSERT_TRUE(ws_json_parse_bool(json, end, "internal", false));
}

void test_json_parse_bool_false(void) {
    const char *json = "{\"internal\":false}";
    const char *end = json + strlen(json);
    TEST_ASSERT_FALSE(ws_json_parse_bool(json, end, "internal", true));
}

void test_json_parse_bool_default(void) {
    const char *json = "{\"other\":true}";
    const char *end = json + strlen(json);
    TEST_ASSERT_TRUE(ws_json_parse_bool(json, end, "internal", true));
    TEST_ASSERT_FALSE(ws_json_parse_bool(json, end, "internal", false));
}

/* ========== JSON Parse Int Tests ========== */

void test_json_parse_int_found(void) {
    const char *json = "{\"pin\":17}";
    const char *end = json + strlen(json);
    TEST_ASSERT_EQUAL_INT(17, ws_json_parse_int(json, end, "pin", 0));
}

void test_json_parse_int_default(void) {
    const char *json = "{\"other\":5}";
    const char *end = json + strlen(json);
    TEST_ASSERT_EQUAL_INT(42, ws_json_parse_int(json, end, "pin", 42));
}

/* ========== JSON Parse Double Tests ========== */

void test_json_parse_double_found(void) {
    const char *json = "{\"latitude\":51.4967}";
    const char *end = json + strlen(json);
    TEST_ASSERT_EQUAL_FLOAT(51.4967,
        ws_json_parse_double(json, end, "latitude", 0.0), 1e-9);
}

void test_json_parse_double_negative(void) {
    const char *json = "{\"longitude\":-0.1764}";
    const char *end = json + strlen(json);
    TEST_ASSERT_EQUAL_FLOAT(-0.1764,
        ws_json_parse_double(json, end, "longitude", 0.0), 1e-9);
}

void test_json_parse_double_integer_value(void) {
    const char *json = "{\"altitude\":12}";
    const char *end = json + strlen(json);
    TEST_ASSERT_EQUAL_FLOAT(12.0,
        ws_json_parse_double(json, end, "altitude", 0.0), 1e-9);
}

void test_json_parse_double_exponent(void) {
    const char *json = "{\"resistance\":1.5e4}";
    const char *end = json + strlen(json);
    TEST_ASSERT_EQUAL_FLOAT(15000.0,
        ws_json_parse_double(json, end, "resistance", 0.0), 1e-6);
}

void test_json_parse_double_default(void) {
    const char *json = "{\"other\":5.0}";
    const char *end = json + strlen(json);
    TEST_ASSERT_EQUAL_FLOAT(99.5,
        ws_json_parse_double(json, end, "latitude", 99.5), 1e-9);
}

/* Present but not a number - must fall back rather than silently yield 0. */
void test_json_parse_double_not_a_number(void) {
    const char *json = "{\"latitude\":\"north\"}";
    const char *end = json + strlen(json);
    TEST_ASSERT_EQUAL_FLOAT(99.5,
        ws_json_parse_double(json, end, "latitude", 99.5), 1e-9);
}

void test_json_parse_double_zero(void) {
    const char *json = "{\"altitude\":0.0}";
    const char *end = json + strlen(json);
    TEST_ASSERT_EQUAL_FLOAT(0.0,
        ws_json_parse_double(json, end, "altitude", 99.5), 1e-9);
}

/* ========== Raw JSON Replacement Tests ========== */

void test_replace_null_raw_object(void) {
    char json[256] = "{\"a\":1,\"location\":null,\"b\":2}";
    ws_json_replace_null_raw(json, sizeof(json), "location",
                             "{\"type\":\"Point\",\"coordinates\":[1.0,2.0]}");
    TEST_ASSERT_EQUAL_STRING(
        "{\"a\":1,\"location\":{\"type\":\"Point\",\"coordinates\":[1.0,2.0]},\"b\":2}",
        json);
}

void test_replace_null_raw_quoted_string(void) {
    char json[128] = "{\"location\":null,\"b\":2}";
    ws_json_replace_null_raw(json, sizeof(json), "location", "\"{{node}}\"");
    TEST_ASSERT_EQUAL_STRING("{\"location\":\"{{node}}\",\"b\":2}", json);
}

void test_replace_null_raw_key_absent(void) {
    char json[128] = "{\"a\":1}";
    ws_json_replace_null_raw(json, sizeof(json), "location", "\"{{node}}\"");
    TEST_ASSERT_EQUAL_STRING("{\"a\":1}", json);
}

/* Already set to something is not null, so it is left alone. */
void test_replace_null_raw_not_null(void) {
    char json[128] = "{\"location\":\"{{none}}\"}";
    ws_json_replace_null_raw(json, sizeof(json), "location", "\"{{node}}\"");
    TEST_ASSERT_EQUAL_STRING("{\"location\":\"{{none}}\"}", json);
}

/* Must refuse rather than overflow the caller's buffer. */
void test_replace_null_raw_too_long_refused(void) {
    char json[32] = "{\"location\":null}";
    ws_json_replace_null_raw(json, sizeof(json), "location",
                             "{\"type\":\"Point\",\"coordinates\":[-0.176400,51.496700,12.00]}");
    TEST_ASSERT_EQUAL_STRING("{\"location\":null}", json);
}

/* config still works through the shared implementation. */
void test_set_config_still_works(void) {
    char json[256] = "{\"value\":1,\"config\":null,\"error\":null}";
    ws_sensor_json_set_config(json, sizeof(json), "{\"software_version\":\"1.0\"}");
    TEST_ASSERT_EQUAL_STRING(
        "{\"value\":1,\"config\":{\"software_version\":\"1.0\"},\"error\":null}", json);
}

/* ========== Sensor Location Tests ========== */

/* Parse the "location" field of a one-entry config object. */
static ws_location_t parse_loc(const char *json) {
    ws_location_t loc;
    const char *end = ws_json_object_end(json);
    ws_parse_sensor_location(json, end, &loc);
    return loc;
}

void test_location_node_token(void) {
    ws_location_t l = parse_loc("{\"hw_id\":\"28-a\",\"location\":\"{{node}}\"}");
    TEST_ASSERT_EQUAL_INT(WS_LOC_NODE, l.source);
}

void test_location_none_token(void) {
    ws_location_t l = parse_loc("{\"hw_id\":\"28-a\",\"location\":\"{{none}}\"}");
    TEST_ASSERT_EQUAL_INT(WS_LOC_NONE, l.source);
}

void test_location_absent_is_undeclared(void) {
    ws_location_t l = parse_loc("{\"hw_id\":\"28-a\",\"internal\":true}");
    TEST_ASSERT_EQUAL_INT(WS_LOC_UNDECLARED, l.source);
}

void test_location_unknown_token_is_undeclared(void) {
    ws_location_t l = parse_loc("{\"location\":\"{{somewhere}}\"}");
    TEST_ASSERT_EQUAL_INT(WS_LOC_UNDECLARED, l.source);
}

void test_location_explicit_lat_lon(void) {
    ws_location_t l = parse_loc(
        "{\"location\":{\"latitude\":51.49674,\"longitude\":-0.17598}}");
    TEST_ASSERT_EQUAL_INT(WS_LOC_EXPLICIT, l.source);
    TEST_ASSERT_EQUAL_FLOAT(51.49674, l.latitude, 1e-9);
    TEST_ASSERT_EQUAL_FLOAT(-0.17598, l.longitude, 1e-9);
    TEST_ASSERT_FALSE(l.has_altitude);
    TEST_ASSERT_FALSE(l.has_accuracy);
}

void test_location_explicit_with_altitude_accuracy(void) {
    ws_location_t l = parse_loc(
        "{\"location\":{\"latitude\":51.5,\"longitude\":-0.17,"
        "\"altitude\":11.85,\"accuracy\":2.0}}");
    TEST_ASSERT_EQUAL_INT(WS_LOC_EXPLICIT, l.source);
    TEST_ASSERT_TRUE(l.has_altitude);
    TEST_ASSERT_TRUE(l.has_accuracy);
    TEST_ASSERT_EQUAL_FLOAT(11.85, l.altitude, 1e-9);
    TEST_ASSERT_EQUAL_FLOAT(2.0, l.accuracy, 1e-9);
}

/* Altitude 0 is a real value, not "absent". */
void test_location_zero_altitude_is_present(void) {
    ws_location_t l = parse_loc(
        "{\"location\":{\"latitude\":51.5,\"longitude\":-0.17,\"altitude\":0}}");
    TEST_ASSERT_EQUAL_INT(WS_LOC_EXPLICIT, l.source);
    TEST_ASSERT_TRUE(l.has_altitude);
    TEST_ASSERT_EQUAL_FLOAT(0.0, l.altitude, 1e-9);
}

/* Equator / prime meridian must not read as absent either. */
void test_location_zero_coordinates(void) {
    ws_location_t l = parse_loc(
        "{\"location\":{\"latitude\":0,\"longitude\":0}}");
    TEST_ASSERT_EQUAL_INT(WS_LOC_EXPLICIT, l.source);
}

void test_location_missing_longitude_rejected(void) {
    ws_location_t l = parse_loc("{\"location\":{\"latitude\":51.5}}");
    TEST_ASSERT_EQUAL_INT(WS_LOC_UNDECLARED, l.source);
}

void test_location_latitude_out_of_range(void) {
    ws_location_t l = parse_loc(
        "{\"location\":{\"latitude\":91.0,\"longitude\":0}}");
    TEST_ASSERT_EQUAL_INT(WS_LOC_UNDECLARED, l.source);
}

void test_location_longitude_out_of_range(void) {
    ws_location_t l = parse_loc(
        "{\"location\":{\"latitude\":0,\"longitude\":-181.0}}");
    TEST_ASSERT_EQUAL_INT(WS_LOC_UNDECLARED, l.source);
}

void test_location_negative_accuracy_rejected(void) {
    ws_location_t l = parse_loc(
        "{\"location\":{\"latitude\":51.5,\"longitude\":-0.17,\"accuracy\":-1}}");
    TEST_ASSERT_EQUAL_INT(WS_LOC_UNDECLARED, l.source);
}

/* Boundary values are valid, not out of range. */
void test_location_boundary_values_accepted(void) {
    ws_location_t l = parse_loc(
        "{\"location\":{\"latitude\":-90,\"longitude\":180,\"accuracy\":0}}");
    TEST_ASSERT_EQUAL_INT(WS_LOC_EXPLICIT, l.source);
    TEST_ASSERT_TRUE(l.has_accuracy);
}

void test_location_null_output_rejected(void) {
    TEST_ASSERT_EQUAL_INT(-1, ws_parse_sensor_location("{}", "{}" + 2, NULL));
}

/* ========== Location JSON Tests ========== */

void test_location_json_node(void) {
    ws_location_t l = parse_loc("{\"location\":\"{{node}}\"}");
    char *j = ws_location_json(&l);
    TEST_ASSERT_NOT_NULL(j);
    TEST_ASSERT_EQUAL_STRING("\"{{node}}\"", j);
    free(j);
}

void test_location_json_none(void) {
    ws_location_t l = parse_loc("{\"location\":\"{{none}}\"}");
    char *j = ws_location_json(&l);
    TEST_ASSERT_NOT_NULL(j);
    TEST_ASSERT_EQUAL_STRING("\"{{none}}\"", j);
    free(j);
}

void test_location_json_undeclared_is_null(void) {
    ws_location_t l = parse_loc("{\"hw_id\":\"28-a\"}");
    TEST_ASSERT_NULL(ws_location_json(&l));
}

/* GeoJSON order is [longitude, latitude] - the reverse of how it is written. */
void test_location_json_explicit_lon_lat_order(void) {
    ws_location_t l = parse_loc(
        "{\"location\":{\"latitude\":51.496700,\"longitude\":-0.176400}}");
    char *j = ws_location_json(&l);
    TEST_ASSERT_NOT_NULL(j);
    TEST_ASSERT_EQUAL_STRING(
        "{\"type\":\"Point\",\"coordinates\":[-0.176400,51.496700]}", j);
    free(j);
}

void test_location_json_explicit_with_altitude(void) {
    ws_location_t l = parse_loc(
        "{\"location\":{\"latitude\":51.496700,\"longitude\":-0.176400,"
        "\"altitude\":12.0}}");
    char *j = ws_location_json(&l);
    TEST_ASSERT_NOT_NULL(j);
    TEST_ASSERT_EQUAL_STRING(
        "{\"type\":\"Point\",\"coordinates\":[-0.176400,51.496700,12.00]}", j);
    free(j);
}

void test_location_json_null_input(void) {
    TEST_ASSERT_NULL(ws_location_json(NULL));
}

/* ========== File Reading Tests ========== */

void test_read_file_success(void) {
    write_file("test content");
    char *content = ws_read_file(temp_file_path, NULL);
    TEST_ASSERT_NOT_NULL(content);
    TEST_ASSERT_EQUAL_STRING("test content", content);
    free(content);
}

void test_read_file_with_size(void) {
    write_file("hello");
    size_t size = 0;
    char *content = ws_read_file(temp_file_path, &size);
    TEST_ASSERT_NOT_NULL(content);
    TEST_ASSERT_EQUAL_INT(5, (int)size);
    free(content);
}

void test_read_file_not_found(void) {
    char *content = ws_read_file("/nonexistent/path", NULL);
    TEST_ASSERT_NULL(content);
}

/* ========== JSON null-replacement Tests ========== */

/* The real sc-prototype template ships "internal" as false, not null. */
#define PROTO_INTERNAL_FALSE     "{\"sensor\":null,\"internal\":false,\"error\":null}"

void test_replace_null_bool_from_null(void) {
    char json[256] = "{\"internal\":null,\"error\":null}";
    ws_json_replace_null_bool(json, "internal", true);
    TEST_ASSERT_EQUAL_STRING("{\"internal\":true,\"error\":null}", json);
}

void test_replace_null_bool_over_prototype_false(void) {
    char json[256] = PROTO_INTERNAL_FALSE;
    ws_json_replace_null_bool(json, "internal", true);
    TEST_ASSERT_EQUAL_STRING("{\"sensor\":null,\"internal\":true,\"error\":null}", json);
}

void test_replace_null_bool_false_stays_false(void) {
    char json[256] = PROTO_INTERNAL_FALSE;
    ws_json_replace_null_bool(json, "internal", false);
    TEST_ASSERT_EQUAL_STRING(PROTO_INTERNAL_FALSE, json);
}

void test_replace_null_bool_over_existing_true(void) {
    char json[256] = "{\"internal\":true}";
    ws_json_replace_null_bool(json, "internal", false);
    TEST_ASSERT_EQUAL_STRING("{\"internal\":false}", json);
}

void test_replace_null_bool_absent_key_is_noop(void) {
    char json[256] = PROTO_INTERNAL_FALSE;
    ws_json_replace_null_bool(json, "missing", true);
    TEST_ASSERT_EQUAL_STRING(PROTO_INTERNAL_FALSE, json);
}

/* "internal" must not be matched by a key that merely shares a prefix. */
void test_replace_null_bool_no_prefix_collision(void) {
    char json[256] = "{\"internal_only\":false,\"internal\":false}";
    ws_json_replace_null_bool(json, "internal", true);
    TEST_ASSERT_EQUAL_STRING("{\"internal_only\":false,\"internal\":true}", json);
}

/* ========== JSON Builder Tests ========== */

void test_json_builder_empty_object(void) {
    char buffer[256];
    ws_json_builder_t builder;
    ws_json_builder_init(&builder, buffer, sizeof(buffer));
    ws_json_builder_start(&builder);
    ws_json_builder_end(&builder);
    TEST_ASSERT_EQUAL_STRING("{}", ws_json_builder_get(&builder));
}

void test_json_builder_single_string(void) {
    char buffer[256];
    ws_json_builder_t builder;
    ws_json_builder_init(&builder, buffer, sizeof(buffer));
    ws_json_builder_start(&builder);
    ws_json_builder_add_string(&builder, "name", "test");
    ws_json_builder_end(&builder);
    TEST_ASSERT_EQUAL_STRING("{\"name\":\"test\"}", ws_json_builder_get(&builder));
}

void test_json_builder_multiple_fields(void) {
    char buffer[256];
    ws_json_builder_t builder;
    ws_json_builder_init(&builder, buffer, sizeof(buffer));
    ws_json_builder_start(&builder);
    ws_json_builder_add_string(&builder, "name", "sensor1");
    ws_json_builder_add_int(&builder, "pin", 17);
    ws_json_builder_add_bool(&builder, "active", true);
    ws_json_builder_end(&builder);
    TEST_ASSERT_EQUAL_STRING("{\"name\":\"sensor1\",\"pin\":17,\"active\":true}", 
                             ws_json_builder_get(&builder));
}

void test_json_builder_null_value(void) {
    char buffer[256];
    ws_json_builder_t builder;
    ws_json_builder_init(&builder, buffer, sizeof(buffer));
    ws_json_builder_start(&builder);
    ws_json_builder_add_null(&builder, "error");
    ws_json_builder_end(&builder);
    TEST_ASSERT_EQUAL_STRING("{\"error\":null}", ws_json_builder_get(&builder));
}

void test_json_builder_double_precision(void) {
    char buffer[256];
    ws_json_builder_t builder;
    ws_json_builder_init(&builder, buffer, sizeof(buffer));
    ws_json_builder_start(&builder);
    ws_json_builder_add_double(&builder, "temp", 23.456, 2);
    ws_json_builder_end(&builder);
    TEST_ASSERT_EQUAL_STRING("{\"temp\":23.46}", ws_json_builder_get(&builder));
}

void test_json_builder_escapes_strings(void) {
    char buffer[256];
    ws_json_builder_t builder;
    ws_json_builder_init(&builder, buffer, sizeof(buffer));
    ws_json_builder_start(&builder);
    ws_json_builder_add_string(&builder, "msg", "say \"hi\"");
    ws_json_builder_end(&builder);
    TEST_ASSERT_EQUAL_STRING("{\"msg\":\"say \\\"hi\\\"\"}", ws_json_builder_get(&builder));
}

/* ========== GPIO Validation Tests ========== */

void test_validate_gpio_pin_valid(void) {
    TEST_ASSERT_TRUE(ws_validate_gpio_pin(4));
    TEST_ASSERT_TRUE(ws_validate_gpio_pin(17));
    TEST_ASSERT_TRUE(ws_validate_gpio_pin(27));
}

void test_validate_gpio_pin_invalid(void) {
    TEST_ASSERT_FALSE(ws_validate_gpio_pin(0));
    TEST_ASSERT_FALSE(ws_validate_gpio_pin(1));
    TEST_ASSERT_FALSE(ws_validate_gpio_pin(28));
    TEST_ASSERT_FALSE(ws_validate_gpio_pin(-1));
}

/* ========== JSON Array Builder Tests ========== */

void test_json_array_empty(void) {
    char buffer[256];
    ws_json_array_builder_t builder;
    ws_json_array_init(&builder, buffer, sizeof(buffer));
    ws_json_array_end(&builder);
    TEST_ASSERT_EQUAL_STRING("[]", ws_json_array_get(&builder));
}

void test_json_array_single_item(void) {
    char buffer[256];
    ws_json_array_builder_t builder;
    ws_json_array_init(&builder, buffer, sizeof(buffer));
    ws_json_array_add(&builder, "{\"a\":1}");
    ws_json_array_end(&builder);
    TEST_ASSERT_EQUAL_STRING("[{\"a\":1}]", ws_json_array_get(&builder));
}

void test_json_array_multiple_items(void) {
    char buffer[256];
    ws_json_array_builder_t builder;
    ws_json_array_init(&builder, buffer, sizeof(buffer));
    ws_json_array_add(&builder, "1");
    ws_json_array_add(&builder, "2");
    ws_json_array_add(&builder, "3");
    ws_json_array_end(&builder);
    TEST_ASSERT_EQUAL_STRING("[1,2,3]", ws_json_array_get(&builder));
}

/* ========== Config Builder Tests ========== */

void test_config_base_with_version(void) {
    char buffer[256];
    ws_build_config_base(buffer, sizeof(buffer), "1.2.3");
    ws_config_end(buffer);
    TEST_ASSERT_EQUAL_STRING("{\"software_version\":\"1.2.3\"}", buffer);
}

void test_config_add_string(void) {
    char buffer[256];
    ws_build_config_base(buffer, sizeof(buffer), "1.0");
    ws_config_add_string(buffer, sizeof(buffer), "i2c_addr", "0x76");
    ws_config_end(buffer);
    TEST_ASSERT_EQUAL_STRING("{\"software_version\":\"1.0\",\"i2c_addr\":\"0x76\"}", buffer);
}

void test_config_add_int(void) {
    char buffer[256];
    ws_build_config_base(buffer, sizeof(buffer), "1.0");
    ws_config_add_int(buffer, sizeof(buffer), "pin", 17);
    ws_config_end(buffer);
    TEST_ASSERT_EQUAL_STRING("{\"software_version\":\"1.0\",\"pin\":17}", buffer);
}

void test_config_add_object(void) {
    char buffer[256];
    ws_build_config_base(buffer, sizeof(buffer), "1.0");
    ws_config_add_object(buffer, sizeof(buffer), "calibration", "{\"par_t1\":123}");
    ws_config_end(buffer);
    TEST_ASSERT_EQUAL_STRING("{\"software_version\":\"1.0\",\"calibration\":{\"par_t1\":123}}", buffer);
}

void test_config_multiple_fields(void) {
    char buffer[512];
    ws_build_config_base(buffer, sizeof(buffer), "2.0");
    ws_config_add_string(buffer, sizeof(buffer), "device", "/dev/i2c-1");
    ws_config_add_int(buffer, sizeof(buffer), "address", 118);
    ws_config_add_object(buffer, sizeof(buffer), "calib", "{\"t\":25}");
    ws_config_end(buffer);
    TEST_ASSERT_EQUAL_STRING(
        "{\"software_version\":\"2.0\",\"device\":\"/dev/i2c-1\",\"address\":118,\"calib\":{\"t\":25}}", 
        buffer);
}

/* ========== Main ========== */

int main(void) {
    UNITY_BEGIN();
    
    /* JSON Escape tests */
    RUN_TEST(test_json_escape_simple_string);
    RUN_TEST(test_json_escape_with_quotes);
    RUN_TEST(test_json_escape_with_backslash);
    RUN_TEST(test_json_escape_null_input);
    
    /* JSON Count tests */
    RUN_TEST(test_json_count_objects_empty);
    RUN_TEST(test_json_count_objects_single);
    RUN_TEST(test_json_count_objects_multiple);
    RUN_TEST(test_json_count_objects_null);
    RUN_TEST(test_json_count_objects_brace_in_string);
    RUN_TEST(test_json_count_objects_token_in_string);
    RUN_TEST(test_json_count_objects_nested);
    RUN_TEST(test_json_count_objects_nested_multiple);
    RUN_TEST(test_json_count_objects_escaped_quote);
    RUN_TEST(test_json_count_objects_unterminated);

    RUN_TEST(test_json_object_end_simple);
    RUN_TEST(test_json_object_end_nested);
    RUN_TEST(test_json_object_end_brace_in_string);
    RUN_TEST(test_json_object_end_token_in_string);
    RUN_TEST(test_json_object_end_escaped_quote);
    RUN_TEST(test_json_object_end_leading_whitespace);
    RUN_TEST(test_json_object_end_unterminated);
    RUN_TEST(test_json_object_end_unterminated_string);
    RUN_TEST(test_json_object_end_not_an_object);
    RUN_TEST(test_json_object_end_null);
    RUN_TEST(test_json_object_end_walks_array);

    RUN_TEST(test_json_parse_object_found);
    RUN_TEST(test_json_parse_object_nested_deeper);
    RUN_TEST(test_json_parse_object_not_found);
    RUN_TEST(test_json_parse_object_is_string);
    RUN_TEST(test_json_parse_object_null);
    
    /* JSON Parse String tests */
    RUN_TEST(test_json_parse_string_found);
    RUN_TEST(test_json_parse_string_second_field);
    RUN_TEST(test_json_parse_string_not_found);
    RUN_TEST(test_json_parse_string_rejects_object);
    RUN_TEST(test_json_parse_string_rejects_number);
    RUN_TEST(test_json_parse_string_rejects_bool);
    RUN_TEST(test_json_parse_string_rejects_array);
    RUN_TEST(test_json_parse_string_token_value);
    RUN_TEST(test_json_parse_string_whitespace_before_value);
    RUN_TEST(test_json_parse_string_empty_value);
    RUN_TEST(test_json_parse_string_escaped_quote);
    RUN_TEST(test_json_parse_string_unterminated_value);
    
    /* JSON Parse Bool tests */
    RUN_TEST(test_json_parse_bool_true);
    RUN_TEST(test_json_parse_bool_false);
    RUN_TEST(test_json_parse_bool_default);
    
    /* JSON Parse Int tests */
    RUN_TEST(test_json_parse_int_found);
    RUN_TEST(test_json_parse_int_default);

    RUN_TEST(test_json_parse_double_found);
    RUN_TEST(test_json_parse_double_negative);
    RUN_TEST(test_json_parse_double_integer_value);
    RUN_TEST(test_json_parse_double_exponent);
    RUN_TEST(test_json_parse_double_default);
    RUN_TEST(test_json_parse_double_not_a_number);
    RUN_TEST(test_json_parse_double_zero);
    
    /* File Reading tests */
    RUN_TEST(test_replace_null_raw_object);
    RUN_TEST(test_replace_null_raw_quoted_string);
    RUN_TEST(test_replace_null_raw_key_absent);
    RUN_TEST(test_replace_null_raw_not_null);
    RUN_TEST(test_replace_null_raw_too_long_refused);
    RUN_TEST(test_set_config_still_works);

    RUN_TEST(test_location_node_token);
    RUN_TEST(test_location_none_token);
    RUN_TEST(test_location_absent_is_undeclared);
    RUN_TEST(test_location_unknown_token_is_undeclared);
    RUN_TEST(test_location_explicit_lat_lon);
    RUN_TEST(test_location_explicit_with_altitude_accuracy);
    RUN_TEST(test_location_zero_altitude_is_present);
    RUN_TEST(test_location_zero_coordinates);
    RUN_TEST(test_location_missing_longitude_rejected);
    RUN_TEST(test_location_latitude_out_of_range);
    RUN_TEST(test_location_longitude_out_of_range);
    RUN_TEST(test_location_negative_accuracy_rejected);
    RUN_TEST(test_location_boundary_values_accepted);
    RUN_TEST(test_location_null_output_rejected);

    RUN_TEST(test_location_json_node);
    RUN_TEST(test_location_json_none);
    RUN_TEST(test_location_json_undeclared_is_null);
    RUN_TEST(test_location_json_explicit_lon_lat_order);
    RUN_TEST(test_location_json_explicit_with_altitude);
    RUN_TEST(test_location_json_null_input);

    RUN_TEST(test_read_file_success);
    RUN_TEST(test_read_file_with_size);
    RUN_TEST(test_read_file_not_found);
    
    /* JSON null-replacement tests */
    RUN_TEST(test_replace_null_bool_from_null);
    RUN_TEST(test_replace_null_bool_over_prototype_false);
    RUN_TEST(test_replace_null_bool_false_stays_false);
    RUN_TEST(test_replace_null_bool_over_existing_true);
    RUN_TEST(test_replace_null_bool_absent_key_is_noop);
    RUN_TEST(test_replace_null_bool_no_prefix_collision);
    
    /* JSON Builder tests */
    RUN_TEST(test_json_builder_empty_object);
    RUN_TEST(test_json_builder_single_string);
    RUN_TEST(test_json_builder_multiple_fields);
    RUN_TEST(test_json_builder_null_value);
    RUN_TEST(test_json_builder_double_precision);
    RUN_TEST(test_json_builder_escapes_strings);
    
    /* GPIO validation tests */
    RUN_TEST(test_validate_gpio_pin_valid);
    RUN_TEST(test_validate_gpio_pin_invalid);
    
    /* JSON Array Builder tests */
    RUN_TEST(test_json_array_empty);
    RUN_TEST(test_json_array_single_item);
    RUN_TEST(test_json_array_multiple_items);
    
    /* Config Builder tests */
    RUN_TEST(test_config_base_with_version);
    RUN_TEST(test_config_add_string);
    RUN_TEST(test_config_add_int);
    RUN_TEST(test_config_add_object);
    RUN_TEST(test_config_multiple_fields);
    
    return UNITY_END();
}
