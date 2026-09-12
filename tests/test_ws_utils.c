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
#include <sys/stat.h>   /* chmod, for the unreadable-file test */
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

/* ========== Node Geolocation Tests ========== */

/* Write a geolocation file and point the reader at it. */
static ws_geolocation_t read_geo(const char *content) {
    ws_geolocation_t g;
    write_file(content);
    setenv("GEOLOC_FILE", temp_file_path, 1);
    ws_read_geolocation(&g);
    unsetenv("GEOLOC_FILE");
    return g;
}

void test_geo_full_four_values(void) {
    ws_geolocation_t g = read_geo("51.4967\n-0.1764\n12.0\n5.0\n");
    TEST_ASSERT_TRUE(g.valid);
    TEST_ASSERT_EQUAL_FLOAT(51.4967, g.latitude, 1e-9);
    TEST_ASSERT_EQUAL_FLOAT(-0.1764, g.longitude, 1e-9);
    TEST_ASSERT_TRUE(g.has_altitude);
    TEST_ASSERT_TRUE(g.has_accuracy);
    TEST_ASSERT_EQUAL_FLOAT(12.0, g.altitude, 1e-9);
    TEST_ASSERT_EQUAL_FLOAT(5.0, g.accuracy, 1e-9);
}

/* The real GeoClue file has inline comments and stray whitespace. */
void test_geo_comments_and_whitespace(void) {
    ws_geolocation_t g = read_geo(
        "# WildlifeSystems node location\n"
        "   51.4967   # latitude\n"
        "\n"
        "-0.1764\t# longitude\n"
        "12.0 # altitude\n"
        "5.0  # accuracy\n");
    TEST_ASSERT_TRUE(g.valid);
    TEST_ASSERT_EQUAL_FLOAT(51.4967, g.latitude, 1e-9);
    TEST_ASSERT_EQUAL_FLOAT(-0.1764, g.longitude, 1e-9);
}

void test_geo_crlf(void) {
    ws_geolocation_t g = read_geo("51.4967\r\n-0.1764\r\n12.0\r\n5.0\r\n");
    TEST_ASSERT_TRUE(g.valid);
    TEST_ASSERT_EQUAL_FLOAT(51.4967, g.latitude, 1e-9);
    TEST_ASSERT_EQUAL_FLOAT(12.0, g.altitude, 1e-9);
}

/* Fewer than four values: altitude and accuracy simply unknown. */
void test_geo_lat_lon_only(void) {
    ws_geolocation_t g = read_geo("51.4967\n-0.1764\n");
    TEST_ASSERT_TRUE(g.valid);
    TEST_ASSERT_FALSE(g.has_altitude);
    TEST_ASSERT_FALSE(g.has_accuracy);
}

void test_geo_three_values(void) {
    ws_geolocation_t g = read_geo("51.4967\n-0.1764\n12.0\n");
    TEST_ASSERT_TRUE(g.valid);
    TEST_ASSERT_TRUE(g.has_altitude);
    TEST_ASSERT_FALSE(g.has_accuracy);
}

/* Latitude alone is not a location. */
void test_geo_one_value_invalid(void) {
    ws_geolocation_t g = read_geo("51.4967\n");
    TEST_ASSERT_FALSE(g.valid);
}

void test_geo_missing_file(void) {
    ws_geolocation_t g;
    setenv("GEOLOC_FILE", "/nonexistent/ws-test-geolocation", 1);
    /* Absent is not an error: an unsurveyed node is a normal state. */
    TEST_ASSERT_EQUAL_INT(0, ws_read_geolocation(&g));
    TEST_ASSERT_FALSE(g.valid);
    unsetenv("GEOLOC_FILE");
}

/* Present but unreadable is a different case from absent: /etc/geolocation is
   mode 0600 by design, so an unprivileged caller must not be given the same
   silence as an unsurveyed node. The call still succeeds with valid = 0 -
   losing the location must not stop a sensor reporting - but it warns.
   Skipped when running as root, which can read the file regardless. */
void test_geo_unreadable_file_is_not_absent(void) {
    ws_geolocation_t g;

    /* This stub Unity has no ignore mechanism, so an inapplicable case simply
       returns. Each skip says why, rather than passing silently. */
    if (geteuid() == 0) {
        printf("  (skipped: running as root)\n");
        return;
    }

    write_file("51.4967\n-0.1764\n12.0\n5.0\n");
    if (chmod(temp_file_path, 0) != 0) {
        printf("  (skipped: chmod unsupported here)\n");
        return;
    }
    /* Confirm the premise before relying on it: a filesystem that ignores mode
       bits would make this test silently vacuous. */
    if (access(temp_file_path, R_OK) == 0) {
        printf("  (skipped: filesystem ignores mode bits)\n");
        chmod(temp_file_path, 0600);
        return;
    }

    setenv("GEOLOC_FILE", temp_file_path, 1);
    TEST_ASSERT_EQUAL_INT(0, ws_read_geolocation(&g));
    TEST_ASSERT_FALSE(g.valid);
    unsetenv("GEOLOC_FILE");

    chmod(temp_file_path, 0600);  /* so tearDown can unlink it */
}

void test_geo_comment_only_file(void) {
    ws_geolocation_t g = read_geo("# nothing here yet\n\n");
    TEST_ASSERT_FALSE(g.valid);
}

void test_geo_latitude_out_of_range(void) {
    ws_geolocation_t g = read_geo("91.0\n-0.1764\n");
    TEST_ASSERT_FALSE(g.valid);
}

void test_geo_longitude_out_of_range(void) {
    ws_geolocation_t g = read_geo("51.4967\n-181.0\n");
    TEST_ASSERT_FALSE(g.valid);
}

void test_geo_negative_accuracy(void) {
    ws_geolocation_t g = read_geo("51.4967\n-0.1764\n12.0\n-1.0\n");
    TEST_ASSERT_FALSE(g.valid);
}

void test_geo_zero_coordinates_valid(void) {
    ws_geolocation_t g = read_geo("0\n0\n");
    TEST_ASSERT_TRUE(g.valid);
}

void test_geo_null_output_rejected(void) {
    TEST_ASSERT_EQUAL_INT(-1, ws_read_geolocation(NULL));
}

void test_geo_geojson_lon_lat_order(void) {
    ws_geolocation_t g = read_geo("51.496700\n-0.176400\n12.0\n");
    char *j = ws_geolocation_geojson(&g);
    TEST_ASSERT_NOT_NULL(j);
    TEST_ASSERT_EQUAL_STRING(
        "{\"type\":\"Point\",\"coordinates\":[-0.176400,51.496700,12.00]}", j);
    free(j);
}

void test_geo_geojson_no_altitude(void) {
    ws_geolocation_t g = read_geo("51.496700\n-0.176400\n");
    char *j = ws_geolocation_geojson(&g);
    TEST_ASSERT_NOT_NULL(j);
    TEST_ASSERT_EQUAL_STRING(
        "{\"type\":\"Point\",\"coordinates\":[-0.176400,51.496700]}", j);
    free(j);
}

void test_geo_geojson_invalid_is_null(void) {
    ws_geolocation_t g;
    memset(&g, 0, sizeof(g));
    TEST_ASSERT_NULL(ws_geolocation_geojson(&g));
    TEST_ASSERT_NULL(ws_geolocation_geojson(NULL));
}

/* {{node}} resolves to the node's position at read time. */
void test_location_json_node_resolves(void) {
    ws_location_t l;
    char *j;
    const char *json = "{\"location\":\"{{node}}\"}";
    const char *end = ws_json_object_end(json);

    write_file("51.496700\n-0.176400\n12.0\n");
    setenv("GEOLOC_FILE", temp_file_path, 1);
    ws_parse_sensor_location(json, end, &l);
    j = ws_location_json(&l);
    unsetenv("GEOLOC_FILE");

    TEST_ASSERT_NOT_NULL(j);
    TEST_ASSERT_EQUAL_STRING(
        "{\"type\":\"Point\",\"coordinates\":[-0.176400,51.496700,12.00]}", j);
    free(j);
}

/* With no node location, the token survives rather than being dropped. */
void test_location_json_node_unresolved_keeps_token(void) {
    ws_location_t l;
    char *j;
    const char *json = "{\"location\":\"{{node}}\"}";
    const char *end = ws_json_object_end(json);

    setenv("GEOLOC_FILE", "/nonexistent/ws-test-geolocation", 1);
    ws_parse_sensor_location(json, end, &l);
    j = ws_location_json(&l);
    unsetenv("GEOLOC_FILE");

    TEST_ASSERT_NOT_NULL(j);
    TEST_ASSERT_EQUAL_STRING("\"{{node}}\"", j);
    free(j);
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

/* Counts non-overlapping occurrences of needle in haystack. */
static size_t count_occurrences(const char *haystack, const char *needle) {
    size_t n = 0;
    size_t len = strlen(needle);
    const char *p = haystack;
    while ((p = strstr(p, needle)) != NULL) {
        n++;
        p += len;
    }
    return n;
}

/* ========== Mock Command Tests ========== */

/* Argument validation only: the rest of ws_cmd_mock writes to stdout and
   depends on sc-prototype being installed, so it is covered by running the
   drivers rather than from here. */
void test_cmd_mock_rejects_no_readings(void) {
    static const ws_mock_reading_t one[] = {
        { "s", "temperature", NULL, WS_UNIT_CELSIUS, 1.0, 1 },
    };
    TEST_ASSERT_EQUAL_INT(WS_EXIT_INVALID_ARG,
        ws_cmd_mock("dev", "dev_mock", "Mock", NULL, 1));
    TEST_ASSERT_EQUAL_INT(WS_EXIT_INVALID_ARG,
        ws_cmd_mock("dev", "dev_mock", "Mock", one, 0));
}

/* ========== Boot Configuration Tests ========== */

/* Writes a boot config and points the library at it. */
static const char *write_boot_config(const char *body) {
    static char path[] = "test_boot_config.txt";
    FILE *fp = fopen(path, "w");
    if (!fp) return NULL;
    fputs(body, fp);
    fclose(fp);
    setenv("WS_BOOT_CONFIG_FILE", path, 1);
    return path;
}

void test_boot_config_path_honours_override(void) {
    const char *path = write_boot_config("# empty\n");
    TEST_ASSERT_NOT_NULL(path);
    TEST_ASSERT_EQUAL_STRING(path, ws_boot_config_path());
    unsetenv("WS_BOOT_CONFIG_FILE");
    remove(path);
}

void test_boot_config_has_finds_directive(void) {
    const char *path = write_boot_config("dtparam=audio=on\n"
                                         "dtoverlay=w1-gpio,gpiopin=17,pullup=1\n");
    TEST_ASSERT_NOT_NULL(path);
    /* A prefix match, so the parameters do not have to be guessed. */
    TEST_ASSERT_EQUAL_INT(1, ws_boot_config_has(path, "dtoverlay=w1-gpio"));
    TEST_ASSERT_EQUAL_INT(0, ws_boot_config_has(path, "dtparam=i2c_arm"));
    unsetenv("WS_BOOT_CONFIG_FILE");
    remove(path);
}

/* A commented-out directive is not enabled, however it is indented. */
void test_boot_config_has_skips_comments(void) {
    const char *path = write_boot_config("# dtoverlay=w1-gpio\n"
                                         "   \t# dtparam=i2c_arm=on\n");
    TEST_ASSERT_NOT_NULL(path);
    TEST_ASSERT_EQUAL_INT(0, ws_boot_config_has(path, "dtoverlay=w1-gpio"));
    TEST_ASSERT_EQUAL_INT(0, ws_boot_config_has(path, "dtparam=i2c_arm"));
    unsetenv("WS_BOOT_CONFIG_FILE");
    remove(path);
}

void test_boot_config_has_ignores_leading_whitespace(void) {
    const char *path = write_boot_config("  \tdtparam=i2c_arm=on\n");
    TEST_ASSERT_NOT_NULL(path);
    TEST_ASSERT_EQUAL_INT(1, ws_boot_config_has(path, "dtparam=i2c_arm"));
    unsetenv("WS_BOOT_CONFIG_FILE");
    remove(path);
}

void test_boot_config_has_missing_file(void) {
    TEST_ASSERT_EQUAL_INT(-1, ws_boot_config_has("/nonexistent/config.txt", "x"));
}

/* A file with no [all] gains one, so the directive is not confined to a
   model-specific section that happens to be last. */
void test_boot_config_add_creates_all_section(void) {
    const char *path = write_boot_config("[pi4]\ndtparam=audio=on\n");
    char *body;
    TEST_ASSERT_NOT_NULL(path);

    TEST_ASSERT_EQUAL_INT(0, ws_boot_config_add(path, "dtparam=i2c_arm=on", "I2C"));

    body = ws_read_file(path, NULL);
    TEST_ASSERT_NOT_NULL(body);
    TEST_ASSERT_NOT_NULL(strstr(body, "[all]"));
    TEST_ASSERT_NOT_NULL(strstr(body, "# I2C"));
    TEST_ASSERT_NOT_NULL(strstr(body, "dtparam=i2c_arm=on"));
    /* And it is now findable. */
    TEST_ASSERT_EQUAL_INT(1, ws_boot_config_has(path, "dtparam=i2c_arm"));
    free(body);
    unsetenv("WS_BOOT_CONFIG_FILE");
    remove(path);
}

void test_boot_config_add_reuses_existing_all_section(void) {
    const char *path = write_boot_config("[all]\ndtparam=audio=on\n");
    char *body;
    TEST_ASSERT_NOT_NULL(path);

    TEST_ASSERT_EQUAL_INT(0, ws_boot_config_add(path, "dtparam=i2c_arm=on", NULL));

    body = ws_read_file(path, NULL);
    TEST_ASSERT_NOT_NULL(body);
    TEST_ASSERT_EQUAL_INT(1, (int)count_occurrences(body, "[all]"));
    free(body);
    unsetenv("WS_BOOT_CONFIG_FILE");
    remove(path);
}

/* The whole "enable" command: adds on the first run, reports on the second. */
void test_enable_boot_config_is_idempotent(void) {
    const char *path = write_boot_config("[all]\n");
    char *body;
    TEST_ASSERT_NOT_NULL(path);

    TEST_ASSERT_EQUAL_INT(WS_EXIT_SUCCESS,
        ws_cmd_enable_boot_config("dtparam=i2c_arm=on", "dtparam=i2c_arm",
                                  "I2C interface", "test"));
    TEST_ASSERT_EQUAL_INT(WS_EXIT_SUCCESS,
        ws_cmd_enable_boot_config("dtparam=i2c_arm=on", "dtparam=i2c_arm",
                                  "I2C interface", "test"));

    body = ws_read_file(path, NULL);
    TEST_ASSERT_NOT_NULL(body);
    /* Added once, not twice. */
    TEST_ASSERT_EQUAL_INT(1, (int)count_occurrences(body, "dtparam=i2c_arm=on"));
    free(body);
    unsetenv("WS_BOOT_CONFIG_FILE");
    remove(path);
}

/* ========== Unit canonicalisation Tests ========== */

void test_unit_canonical_exact(void) {
    TEST_ASSERT_EQUAL_STRING(WS_UNIT_CELSIUS,    ws_unit_canonical("Celsius"));
    TEST_ASSERT_EQUAL_STRING(WS_UNIT_PERCENTAGE, ws_unit_canonical("percentage"));
    TEST_ASSERT_EQUAL_STRING(WS_UNIT_HPA,        ws_unit_canonical("hPa"));
    TEST_ASSERT_EQUAL_STRING(WS_UNIT_OHMS,       ws_unit_canonical("Ohms"));
}

/* A shell caller should not have to match our capitalisation. */
void test_unit_canonical_ignores_case(void) {
    TEST_ASSERT_EQUAL_STRING(WS_UNIT_CELSIUS,    ws_unit_canonical("celsius"));
    TEST_ASSERT_EQUAL_STRING(WS_UNIT_CELSIUS,    ws_unit_canonical("CELSIUS"));
    TEST_ASSERT_EQUAL_STRING(WS_UNIT_PERCENTAGE, ws_unit_canonical("PERCENTAGE"));
    TEST_ASSERT_EQUAL_STRING(WS_UNIT_HPA,        ws_unit_canonical("hpa"));
    TEST_ASSERT_EQUAL_STRING(WS_UNIT_OHMS,       ws_unit_canonical("ohms"));
}

/* The spellings that actually shipped wrong in this project, and near misses. */
void test_unit_canonical_rejects_misspellings(void) {
    TEST_ASSERT_NULL(ws_unit_canonical("percant"));
    TEST_ASSERT_NULL(ws_unit_canonical("percent"));
    TEST_ASSERT_NULL(ws_unit_canonical("C"));
    TEST_ASSERT_NULL(ws_unit_canonical("degrees"));
    TEST_ASSERT_NULL(ws_unit_canonical("ohm"));
    TEST_ASSERT_NULL(ws_unit_canonical(""));
    TEST_ASSERT_NULL(ws_unit_canonical(NULL));
}

/* ========== Sensor config iterator Tests ========== */

/* Writes a temporary config file and returns its path. */
static const char *write_cfg(const char *body) {
    static char path[] = "test_cfg_iter.json";
    FILE *fp = fopen(path, "w");
    if (!fp) return NULL;
    fputs(body, fp);
    fclose(fp);
    return path;
}

void test_config_iter_missing_file_is_not_an_error(void) {
    ws_config_iter_t it;
    TEST_ASSERT_EQUAL_INT(0, ws_config_iter_open(&it, "/nonexistent/sensors.json"));
    ws_config_iter_close(&it);
}

void test_config_iter_null_path_rejected(void) {
    ws_config_iter_t it;
    TEST_ASSERT_EQUAL_INT(-1, ws_config_iter_open(&it, NULL));
    ws_config_iter_close(&it);
}

void test_config_iter_counts_and_yields_entries(void) {
    ws_sensor_config_base_t base;
    ws_config_iter_t it;
    const char *start, *end;
    const char *path = write_cfg(
        "[{\"pin\":4,\"internal\":true,\"sensor_id\":\"A\",\"sensor_name\":\"One\"},"
        " {\"pin\":17,\"sensor_id\":\"B\"}]");
    TEST_ASSERT_NOT_NULL(path);

    TEST_ASSERT_EQUAL_INT(2, ws_config_iter_open(&it, path));

    TEST_ASSERT_TRUE(ws_config_iter_next(&it, &base, &start, &end));
    TEST_ASSERT_TRUE(base.internal);
    TEST_ASSERT_EQUAL_STRING("A", base.sensor_id);
    TEST_ASSERT_EQUAL_STRING("One", base.sensor_name);
    TEST_ASSERT_EQUAL_INT(4, ws_json_parse_int(start, end, "pin", 0));
    ws_sensor_config_free_fields(&base);

    /* Absent fields stay zeroed rather than carrying over from the last entry. */
    TEST_ASSERT_TRUE(ws_config_iter_next(&it, &base, &start, &end));
    TEST_ASSERT_FALSE(base.internal);
    TEST_ASSERT_EQUAL_STRING("B", base.sensor_id);
    TEST_ASSERT_NULL(base.sensor_name);
    TEST_ASSERT_EQUAL_INT(17, ws_json_parse_int(start, end, "pin", 0));
    ws_sensor_config_free_fields(&base);

    TEST_ASSERT_FALSE(ws_config_iter_next(&it, &base, &start, &end));
    ws_config_iter_close(&it);
    remove(path);
}

/* A nested "location" object must not be mistaken for another entry, nor
   truncate the entry that contains it. */
void test_config_iter_handles_nested_location(void) {
    ws_sensor_config_base_t base;
    ws_config_iter_t it;
    const char *start, *end;
    const char *path = write_cfg(
        "[{\"hw_id\":\"28-a\",\"location\":{\"latitude\":51.5,\"longitude\":-0.12},"
        "\"sensor_name\":\"After the object\"}]");
    TEST_ASSERT_NOT_NULL(path);

    TEST_ASSERT_EQUAL_INT(1, ws_config_iter_open(&it, path));
    TEST_ASSERT_TRUE(ws_config_iter_next(&it, &base, &start, &end));
    TEST_ASSERT_EQUAL_INT(WS_LOC_EXPLICIT, base.location.source);
    TEST_ASSERT_EQUAL_FLOAT(51.5, base.location.latitude, 0.0001);
    /* A field after the nested object is still in range. */
    TEST_ASSERT_EQUAL_STRING("After the object", base.sensor_name);
    ws_sensor_config_free_fields(&base);

    TEST_ASSERT_FALSE(ws_config_iter_next(&it, &base, &start, &end));
    ws_config_iter_close(&it);
    remove(path);
}

void test_config_iter_free_fields_nulls_pointers(void) {
    ws_sensor_config_base_t base;
    memset(&base, 0, sizeof(base));
    base.sensor_id = strdup("A");
    base.sensor_name = strdup("B");
    ws_sensor_config_free_fields(&base);
    TEST_ASSERT_NULL(base.sensor_id);
    TEST_ASSERT_NULL(base.sensor_name);
    /* Idempotent, so a double free_config cannot corrupt the heap. */
    ws_sensor_config_free_fields(&base);
}

/* ========== Bounded string replacement Tests ========== */

void test_replace_null_string_inserts_value(void) {
    char json[64] = "{\"sensor_name\":null}";
    ws_json_replace_null_string(json, sizeof(json), "sensor_name", "Pond probe");
    TEST_ASSERT_EQUAL_STRING("{\"sensor_name\":\"Pond probe\"}", json);
}

/* A value that does not fit must be refused, not clipped: a clipped value
   leaves the string unterminated and invalidates the whole document. */
void test_replace_null_string_refuses_when_too_long(void) {
    char json[32] = "{\"sensor_name\":null}";
    char before[32];
    strcpy(before, json);
    ws_json_replace_null_string(json, sizeof(json),
                                "sensor_name", "far too long to ever fit in here");
    TEST_ASSERT_EQUAL_STRING(before, json);
}

/* The longest value that exactly fills the buffer must still be written. */
void test_replace_null_string_exact_fit_accepted(void) {
    /* {"k":null} and {"k":"VV"} are both 10 chars, +1 for the terminator. */
    char json[11] = "{\"k\":null}";
    ws_json_replace_null_string(json, sizeof(json), "k", "VV");
    TEST_ASSERT_EQUAL_STRING("{\"k\":\"VV\"}", json);
}

void test_replace_null_string_one_over_is_refused(void) {
    char json[11] = "{\"k\":null}";
    ws_json_replace_null_string(json, sizeof(json), "k", "VVV");
    TEST_ASSERT_EQUAL_STRING("{\"k\":null}", json);
}

void test_replace_null_string_absent_key_is_noop(void) {
    char json[64] = "{\"sensor_name\":null}";
    ws_json_replace_null_string(json, sizeof(json), "missing", "x");
    TEST_ASSERT_EQUAL_STRING("{\"sensor_name\":null}", json);
}

/* A long error message must not be able to corrupt the reading either. */
void test_set_error_refuses_oversized_message(void) {
    char json[48] = "{\"value\":null,\"error\":null}";
    ws_sensor_json_set_error(json, sizeof(json),
                             "a considerably longer error message than will fit");
    TEST_ASSERT_EQUAL_STRING("{\"value\":null,\"error\":null}", json);
}

/* ========== Reading outcome (value XOR error) Tests ========== */

/* A reading's JSON after ws_build_sensor_json_base(), value and error still null. */
#define READING_TEMPLATE "{\"value\":null,\"internal\":false,\"error\":null}"

void test_set_result_success_sets_value_only(void) {
    char json[256] = READING_TEMPLATE;
    ws_sensor_json_set_result(json, sizeof(json), 21.375, 3, NULL);
    TEST_ASSERT_EQUAL_STRING("{\"value\":21.375,\"internal\":false,\"error\":null}", json);
}

void test_set_result_error_leaves_value_null(void) {
    char json[256] = READING_TEMPLATE;
    /* The w1therm case: a sentinel temperature must not reach "value". */
    ws_sensor_json_set_result(json, sizeof(json), 85.0, 3, "Sensor has startup value (85.000 C)");
    TEST_ASSERT_NOT_NULL(strstr(json, "\"value\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"error\":\"Sensor has startup value (85.000 C)\""));
    TEST_ASSERT_NULL(strstr(json, "85.000,"));
}

void test_set_result_empty_error_counts_as_success(void) {
    char json[256] = READING_TEMPLATE;
    ws_sensor_json_set_result(json, sizeof(json), 1.5, 1, "");
    TEST_ASSERT_EQUAL_STRING("{\"value\":1.5,\"internal\":false,\"error\":null}", json);
}

void test_set_result_escapes_error(void) {
    char json[256] = READING_TEMPLATE;
    ws_sensor_json_set_result(json, sizeof(json), 0.0, 1, "bad \"quoted\" value");
    TEST_ASSERT_NOT_NULL(strstr(json, "\"error\":\"bad \\\"quoted\\\" value\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"value\":null"));
}

void test_set_result_null_json_is_noop(void) {
    char json[256] = READING_TEMPLATE;
    /* Must not dereference a NULL buffer on either branch. */
    ws_sensor_json_set_result(NULL, 256, 1.0, 1, NULL);
    ws_sensor_json_set_result(NULL, 256, 1.0, 1, "boom");
    /* Reaching here without crashing is the assertion; confirm it still works. */
    ws_sensor_json_set_result(json, sizeof(json), 2.5, 1, NULL);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"value\":2.5"));
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

/* Counts occurrences of c in s. */
static size_t count_char(const char *s, char c) {
    size_t n = 0;
    while (*s) {
        if (*s++ == c) n++;
    }
    return n;
}

void test_json_array_empty(void) {
    ws_json_array_builder_t builder;
    TEST_ASSERT_EQUAL_INT(0, ws_json_array_init(&builder));
    ws_json_array_end(&builder);
    TEST_ASSERT_EQUAL_STRING("[]", ws_json_array_get(&builder));
    ws_json_array_free(&builder);
}

void test_json_array_single_item(void) {
    ws_json_array_builder_t builder;
    TEST_ASSERT_EQUAL_INT(0, ws_json_array_init(&builder));
    ws_json_array_add(&builder, "{\"a\":1}");
    ws_json_array_end(&builder);
    TEST_ASSERT_EQUAL_STRING("[{\"a\":1}]", ws_json_array_get(&builder));
    ws_json_array_free(&builder);
}

void test_json_array_multiple_items(void) {
    ws_json_array_builder_t builder;
    TEST_ASSERT_EQUAL_INT(0, ws_json_array_init(&builder));
    ws_json_array_add(&builder, "1");
    ws_json_array_add(&builder, "2");
    ws_json_array_add(&builder, "3");
    ws_json_array_end(&builder);
    TEST_ASSERT_EQUAL_STRING("[1,2,3]", ws_json_array_get(&builder));
    ws_json_array_free(&builder);
}

/* The point of the rewrite: no caller has to guess a capacity up front. */
void test_json_array_grows_past_initial_capacity(void) {
    ws_json_array_builder_t builder;
    char item[512];
    const char *out;
    int i;

    memset(item, 'x', sizeof(item));
    item[0] = '"';
    item[sizeof(item) - 2] = '"';
    item[sizeof(item) - 1] = 0;

    TEST_ASSERT_EQUAL_INT(0, ws_json_array_init(&builder));
    /* 64 items of ~511 bytes is far beyond the initial 4096. */
    for (i = 0; i < 64; i++) {
        ws_json_array_add(&builder, item);
    }
    ws_json_array_end(&builder);

    out = ws_json_array_get(&builder);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_INT('[', out[0]);
    TEST_ASSERT_EQUAL_INT(']', out[strlen(out) - 1]);
    TEST_ASSERT_EQUAL_INT(64, builder.item_count);
    TEST_ASSERT_EQUAL_INT(63, (int)count_char(out, ','));
    ws_json_array_free(&builder);
}

void test_json_array_free_is_idempotent(void) {
    ws_json_array_builder_t builder;
    TEST_ASSERT_EQUAL_INT(0, ws_json_array_init(&builder));
    ws_json_array_add(&builder, "1");
    ws_json_array_free(&builder);
    ws_json_array_free(&builder);
    TEST_ASSERT_NULL(builder.buffer);
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

    RUN_TEST(test_geo_full_four_values);
    RUN_TEST(test_geo_comments_and_whitespace);
    RUN_TEST(test_geo_crlf);
    RUN_TEST(test_geo_lat_lon_only);
    RUN_TEST(test_geo_three_values);
    RUN_TEST(test_geo_one_value_invalid);
    RUN_TEST(test_geo_missing_file);
    RUN_TEST(test_geo_unreadable_file_is_not_absent);
    RUN_TEST(test_geo_comment_only_file);
    RUN_TEST(test_geo_latitude_out_of_range);
    RUN_TEST(test_geo_longitude_out_of_range);
    RUN_TEST(test_geo_negative_accuracy);
    RUN_TEST(test_geo_zero_coordinates_valid);
    RUN_TEST(test_geo_null_output_rejected);
    RUN_TEST(test_geo_geojson_lon_lat_order);
    RUN_TEST(test_geo_geojson_no_altitude);
    RUN_TEST(test_geo_geojson_invalid_is_null);
    RUN_TEST(test_location_json_node_resolves);
    RUN_TEST(test_location_json_node_unresolved_keeps_token);

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
    
    /* Mock command tests */
    RUN_TEST(test_cmd_mock_rejects_no_readings);

    /* Boot configuration tests */
    RUN_TEST(test_boot_config_path_honours_override);
    RUN_TEST(test_boot_config_has_finds_directive);
    RUN_TEST(test_boot_config_has_skips_comments);
    RUN_TEST(test_boot_config_has_ignores_leading_whitespace);
    RUN_TEST(test_boot_config_has_missing_file);
    RUN_TEST(test_boot_config_add_creates_all_section);
    RUN_TEST(test_boot_config_add_reuses_existing_all_section);
    RUN_TEST(test_enable_boot_config_is_idempotent);

    /* Unit canonicalisation tests */
    RUN_TEST(test_unit_canonical_exact);
    RUN_TEST(test_unit_canonical_ignores_case);
    RUN_TEST(test_unit_canonical_rejects_misspellings);

    /* Sensor config iterator tests */
    RUN_TEST(test_config_iter_missing_file_is_not_an_error);
    RUN_TEST(test_config_iter_null_path_rejected);
    RUN_TEST(test_config_iter_counts_and_yields_entries);
    RUN_TEST(test_config_iter_handles_nested_location);
    RUN_TEST(test_config_iter_free_fields_nulls_pointers);

    /* Bounded string replacement tests */
    RUN_TEST(test_replace_null_string_inserts_value);
    RUN_TEST(test_replace_null_string_refuses_when_too_long);
    RUN_TEST(test_replace_null_string_exact_fit_accepted);
    RUN_TEST(test_replace_null_string_one_over_is_refused);
    RUN_TEST(test_replace_null_string_absent_key_is_noop);
    RUN_TEST(test_set_error_refuses_oversized_message);

    /* Reading outcome tests */
    RUN_TEST(test_set_result_success_sets_value_only);
    RUN_TEST(test_set_result_error_leaves_value_null);
    RUN_TEST(test_set_result_empty_error_counts_as_success);
    RUN_TEST(test_set_result_escapes_error);
    RUN_TEST(test_set_result_null_json_is_noop);

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
    RUN_TEST(test_json_array_grows_past_initial_capacity);
    RUN_TEST(test_json_array_free_is_idempotent);
    
    /* Config Builder tests */
    RUN_TEST(test_config_base_with_version);
    RUN_TEST(test_config_add_string);
    RUN_TEST(test_config_add_int);
    RUN_TEST(test_config_add_object);
    RUN_TEST(test_config_multiple_fields);
    
    return UNITY_END();
}
