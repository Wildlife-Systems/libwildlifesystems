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
    
    /* JSON Parse String tests */
    RUN_TEST(test_json_parse_string_found);
    RUN_TEST(test_json_parse_string_second_field);
    RUN_TEST(test_json_parse_string_not_found);
    
    /* JSON Parse Bool tests */
    RUN_TEST(test_json_parse_bool_true);
    RUN_TEST(test_json_parse_bool_false);
    RUN_TEST(test_json_parse_bool_default);
    
    /* JSON Parse Int tests */
    RUN_TEST(test_json_parse_int_found);
    RUN_TEST(test_json_parse_int_default);
    
    /* File Reading tests */
    RUN_TEST(test_read_file_success);
    RUN_TEST(test_read_file_with_size);
    RUN_TEST(test_read_file_not_found);
    
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
