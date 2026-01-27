/*
 * ws_utils.c - WildlifeSystems shared sensor utilities
 * Copyright (C) 2024 Wildlife Systems
 *
 * Common utilities for WildlifeSystems sensor drivers.
 * This code is shared between sensor-dht11, sensor-w1therm, and other sensor drivers.
 */

/* Enable POSIX functions like popen/pclose */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <syslog.h>
#include "ws_utils.h"

/* Cached prototype from sc-prototype */
static char *g_prototype_cache = NULL;
static bool g_prototype_loaded = false;

/* ============================================================================
 * Logging Functions
 * ============================================================================ */

/*
 * Initialize syslog for a sensor program.
 */
void ws_log_init(const char *program_name) {
    openlog(program_name, LOG_PID | LOG_NDELAY, LOG_USER);
}

/*
 * Log an error to both stderr and syslog.
 */
void ws_log_error(const char *fmt, ...) {
    va_list args;
    char buf[512];
    
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    fprintf(stderr, "Error: %s\n", buf);
    syslog(LOG_ERR, "%s", buf);
}

/*
 * Log a warning to both stderr and syslog.
 */
void ws_log_warning(const char *fmt, ...) {
    va_list args;
    char buf[512];
    
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    fprintf(stderr, "Warning: %s\n", buf);
    syslog(LOG_WARNING, "%s", buf);
}

/*
 * Log an info message to syslog only.
 */
void ws_log_info(const char *fmt, ...) {
    va_list args;
    char buf[512];
    
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    syslog(LOG_INFO, "%s", buf);
}

/* ============================================================================
 * Base Config Functions
 * ============================================================================ */

/*
 * Free a base sensor config's string fields.
 */
void ws_sensor_config_free_fields(ws_sensor_config_base_t *config) {
    if (!config) return;
    free(config->sensor_id);
    free(config->sensor_name);
    config->sensor_id = NULL;
    config->sensor_name = NULL;
}

/*
 * Escape a string for safe inclusion in JSON output.
 */
void ws_json_escape_string(const char *src, char *dst, size_t dst_len) {
    size_t i = 0, j = 0;
    
    if (!src || !dst || dst_len == 0) {
        if (dst && dst_len > 0) dst[0] = '\0';
        return;
    }
    
    while (src[i] && j < dst_len - 2) {
        if (src[i] == '"' || src[i] == '\\') {
            dst[j++] = '\\';
        }
        dst[j++] = src[i++];
    }
    dst[j] = '\0';
}

/*
 * Replace a JSON null value with a string value.
 */
void ws_json_replace_null_string(char *json, const char *key, const char *value) {
    char search[128];
    char replace[512];
    char *pos;
    size_t search_len, replace_len, tail_len;
    
    if (!json || !key || !value) return;
    
    snprintf(search, sizeof(search), "\"%s\":null", key);
    snprintf(replace, sizeof(replace), "\"%s\":\"%s\"", key, value);
    
    pos = strstr(json, search);
    if (pos == NULL) {
        return;
    }
    
    search_len = strlen(search);
    replace_len = strlen(replace);
    tail_len = strlen(pos + search_len);
    
    /* Move tail to make room (or shrink) */
    memmove(pos + replace_len, pos + search_len, tail_len + 1);
    /* Copy replacement */
    memcpy(pos, replace, replace_len);
}

/*
 * Replace a JSON null value with a number value.
 */
void ws_json_replace_null_number(char *json, const char *key, double value) {
    char search[128];
    char replace[256];
    char *pos;
    size_t search_len, replace_len, tail_len;
    
    if (!json || !key) return;
    
    snprintf(search, sizeof(search), "\"%s\":null", key);
    snprintf(replace, sizeof(replace), "\"%s\":%.3f", key, value);
    
    pos = strstr(json, search);
    if (pos == NULL) {
        return;
    }
    
    search_len = strlen(search);
    replace_len = strlen(replace);
    tail_len = strlen(pos + search_len);
    
    memmove(pos + replace_len, pos + search_len, tail_len + 1);
    memcpy(pos, replace, replace_len);
}

/*
 * Replace a JSON null value with an integer value.
 */
void ws_json_replace_null_int(char *json, const char *key, long value) {
    char search[128];
    char replace[256];
    char *pos;
    size_t search_len, replace_len, tail_len;
    
    if (!json || !key) return;
    
    snprintf(search, sizeof(search), "\"%s\":null", key);
    snprintf(replace, sizeof(replace), "\"%s\":%ld", key, value);
    
    pos = strstr(json, search);
    if (pos == NULL) {
        return;
    }
    
    search_len = strlen(search);
    replace_len = strlen(replace);
    tail_len = strlen(pos + search_len);
    
    memmove(pos + replace_len, pos + search_len, tail_len + 1);
    memcpy(pos, replace, replace_len);
}

/*
 * Replace a JSON null value with a boolean value.
 */
void ws_json_replace_null_bool(char *json, const char *key, bool value) {
    char search[128];
    char replace[256];
    char *pos;
    size_t search_len, replace_len, tail_len;
    
    if (!json || !key) return;
    
    snprintf(search, sizeof(search), "\"%s\":null", key);
    snprintf(replace, sizeof(replace), "\"%s\":%s", key, value ? "true" : "false");
    
    pos = strstr(json, search);
    if (pos == NULL) {
        return;
    }
    
    search_len = strlen(search);
    replace_len = strlen(replace);
    tail_len = strlen(pos + search_len);
    
    memmove(pos + replace_len, pos + search_len, tail_len + 1);
    memcpy(pos, replace, replace_len);
}

/*
 * Get JSON prototype by calling sc-prototype.
 * Returns dynamically allocated string, caller must free.
 */
char *ws_get_sc_prototype(void) {
    FILE *fp;
    char *buffer = NULL;
    size_t bufsize = 0;
    ssize_t len;
    
    fp = popen("sc-prototype", "r");
    if (fp == NULL) {
        return NULL;
    }
    
    len = getline(&buffer, &bufsize, fp);
    pclose(fp);
    
    if (len < 0) {
        free(buffer);
        return NULL;
    }
    
    /* Remove trailing newline */
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    }
    
    return buffer;
}

/*
 * Get cached JSON prototype template.
 * Returns pointer to internal buffer. Calls sc-prototype on first use.
 */
const char *ws_get_prototype_cached(void) {
    if (g_prototype_loaded && g_prototype_cache) {
        return g_prototype_cache;
    }
    
    g_prototype_cache = ws_get_sc_prototype();
    if (g_prototype_cache == NULL) {
        return NULL;
    }
    
    g_prototype_loaded = true;
    return g_prototype_cache;
}

/*
 * Get current Unix timestamp.
 */
time_t ws_get_timestamp(void) {
    return time(NULL);
}

/*
 * Handle the 'identify' command.
 */
void ws_cmd_identify(void) {
    exit(WS_EXIT_IDENTIFY);
}

/*
 * Handle the 'list' command for single measurement type.
 */
void ws_cmd_list_single(const char *measurement) {
    if (measurement) {
        printf("%s\n", measurement);
    }
    exit(WS_EXIT_SUCCESS);
}

/*
 * Handle the 'list' command for multiple measurement types.
 */
void ws_cmd_list_multiple(const char **measurements) {
    if (measurements) {
        while (*measurements) {
            printf("%s\n", *measurements);
            measurements++;
        }
    }
    exit(WS_EXIT_SUCCESS);
}

/*
 * Get Raspberry Pi serial number from /proc/cpuinfo.
 * Returns dynamically allocated string, caller must free.
 */
char *ws_get_serial_number(void) {
    FILE *fp;
    char *line = NULL;
    size_t line_len = 0;
    char *result = NULL;
    
    fp = fopen("/proc/cpuinfo", "r");
    if (!fp) {
        return NULL;
    }
    
    while (getline(&line, &line_len, fp) != -1) {
        if (strncmp(line, "Serial", 6) == 0) {
            char *colon = strchr(line, ':');
            if (colon) {
                colon++;
                while (*colon == ' ' || *colon == '\t') colon++;
                char *nl = strchr(colon, '\n');
                if (nl) *nl = '\0';
                result = strdup(colon);
                break;
            }
        }
    }
    
    free(line);
    fclose(fp);
    return result;
}

/*
 * Validate GPIO pin is in valid range for Raspberry Pi (2-27).
 */
bool ws_validate_gpio_pin(int pin) {
    return (pin >= 2 && pin <= 27);
}

/*
 * Print version information for a sensor program.
 */
void ws_print_version(const char *program_name, const char *version) {
    printf("%s version %s\n", program_name, version);
    printf("Copyright (C) 2024 Wildlife Systems\n");
}

/*
 * Read entire file into malloc'd buffer.
 * Returns NULL on error. Caller must free the returned buffer.
 */
char *ws_read_file(const char *path, size_t *size_out) {
    FILE *fp;
    char *buffer;
    long file_size;
    size_t bytes_read;
    
    if (size_out) *size_out = 0;
    
    fp = fopen(path, "r");
    if (!fp) return NULL;
    
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (file_size <= 0) {
        fclose(fp);
        return NULL;
    }
    
    buffer = malloc(file_size + 1);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }
    
    bytes_read = fread(buffer, 1, file_size, fp);
    buffer[bytes_read] = '\0';
    fclose(fp);
    
    if (size_out) *size_out = bytes_read;
    return buffer;
}

/*
 * Count JSON objects in a buffer by counting '{' characters.
 */
int ws_json_count_objects(const char *buffer) {
    int count = 0;
    const char *ptr = buffer;
    
    if (!buffer) return 0;
    
    while ((ptr = strchr(ptr, '{')) != NULL) {
        count++;
        ptr++;
    }
    return count;
}

/*
 * Parse a JSON string field from a JSON object.
 */
char *ws_json_parse_string(const char *ptr, const char *end, const char *field) {
    char search[128];
    char *field_ptr;
    char *quote_start;
    char *quote_end;
    
    if (!ptr || !end || !field) return NULL;
    
    snprintf(search, sizeof(search), "\"%s\"", field);
    field_ptr = strstr(ptr, search);
    
    if (!field_ptr || field_ptr >= end) return NULL;
    
    field_ptr = strchr(field_ptr, ':');
    if (!field_ptr || field_ptr >= end) return NULL;
    
    quote_start = strchr(field_ptr, '"');
    if (!quote_start || quote_start >= end) return NULL;
    
    quote_start++;
    quote_end = strchr(quote_start, '"');
    if (!quote_end || quote_end > end) return NULL;
    
    return strndup(quote_start, quote_end - quote_start);
}

/*
 * Parse a JSON boolean field from a JSON object.
 */
bool ws_json_parse_bool(const char *ptr, const char *end, const char *field, bool default_val) {
    char search[128];
    char *field_ptr;
    char *colon;
    
    if (!ptr || !end || !field) return default_val;
    
    snprintf(search, sizeof(search), "\"%s\"", field);
    field_ptr = strstr(ptr, search);
    
    if (!field_ptr || field_ptr >= end) return default_val;
    
    colon = strchr(field_ptr, ':');
    if (!colon || colon >= end) return default_val;
    
    colon++;
    while (*colon == ' ' || *colon == '\t') colon++;
    
    if (colon >= end) return default_val;
    
    if (strncmp(colon, "true", 4) == 0) return true;
    if (strncmp(colon, "false", 5) == 0) return false;
    
    return default_val;
}

/*
 * Parse a JSON integer field from a JSON object.
 */
int ws_json_parse_int(const char *ptr, const char *end, const char *field, int default_val) {
    char search[128];
    char *field_ptr;
    char *colon;
    
    if (!ptr || !end || !field) return default_val;
    
    snprintf(search, sizeof(search), "\"%s\"", field);
    field_ptr = strstr(ptr, search);
    
    if (!field_ptr || field_ptr >= end) return default_val;
    
    colon = strchr(field_ptr, ':');
    if (!colon || colon >= end) return default_val;
    
    colon++;
    while (*colon == ' ' || *colon == '\t') colon++;
    
    if (colon >= end) return default_val;
    
    return atoi(colon);
}

/*
 * Get serial number with suffix appended.
 */
char *ws_get_serial_with_suffix(const char *suffix) {
    char *raw_serial;
    char *result;
    size_t len;
    
    if (!suffix) return NULL;
    
    raw_serial = ws_get_serial_number();
    if (!raw_serial) return NULL;
    
    len = strlen(raw_serial) + strlen(suffix) + 2;
    result = malloc(len);
    if (!result) {
        free(raw_serial);
        return NULL;
    }
    
    snprintf(result, len, "%s_%s", raw_serial, suffix);
    free(raw_serial);
    return result;
}

/* ============================================================================
 * JSON Builder Functions
 * ============================================================================ */

/*
 * Initialize a JSON builder with a buffer.
 */
void ws_json_builder_init(ws_json_builder_t *builder, char *buffer, size_t capacity) {
    if (!builder || !buffer || capacity == 0) return;
    
    builder->buffer = buffer;
    builder->capacity = capacity;
    builder->length = 0;
    builder->field_count = 0;
    builder->error = 0;
    buffer[0] = '\0';
}

/*
 * Append to the builder buffer with overflow checking.
 */
static void builder_append(ws_json_builder_t *builder, const char *str) {
    size_t str_len;
    
    if (!builder || builder->error || !str) return;
    
    str_len = strlen(str);
    if (builder->length + str_len >= builder->capacity) {
        builder->error = 1;
        return;
    }
    
    memcpy(builder->buffer + builder->length, str, str_len + 1);
    builder->length += str_len;
}

/*
 * Start a JSON object.
 */
void ws_json_builder_start(ws_json_builder_t *builder) {
    if (!builder) return;
    builder_append(builder, "{");
    builder->field_count = 0;
}

/*
 * End a JSON object.
 */
void ws_json_builder_end(ws_json_builder_t *builder) {
    if (!builder) return;
    builder_append(builder, "}");
}

/*
 * Add comma separator if needed.
 */
static void builder_add_comma(ws_json_builder_t *builder) {
    if (builder->field_count > 0) {
        builder_append(builder, ",");
    }
    builder->field_count++;
}

/*
 * Add a string field to the JSON object.
 */
void ws_json_builder_add_string(ws_json_builder_t *builder, const char *key, const char *value) {
    char escaped[1024];
    char field[1280];
    
    if (!builder || !key) return;
    
    builder_add_comma(builder);
    
    if (value) {
        ws_json_escape_string(value, escaped, sizeof(escaped));
        snprintf(field, sizeof(field), "\"%s\":\"%s\"", key, escaped);
    } else {
        snprintf(field, sizeof(field), "\"%s\":null", key);
    }
    
    builder_append(builder, field);
}

/*
 * Add an integer field to the JSON object.
 */
void ws_json_builder_add_int(ws_json_builder_t *builder, const char *key, long value) {
    char field[256];
    
    if (!builder || !key) return;
    
    builder_add_comma(builder);
    snprintf(field, sizeof(field), "\"%s\":%ld", key, value);
    builder_append(builder, field);
}

/*
 * Add a double field to the JSON object.
 */
void ws_json_builder_add_double(ws_json_builder_t *builder, const char *key, double value, int precision) {
    char field[256];
    char fmt[32];
    
    if (!builder || !key) return;
    
    builder_add_comma(builder);
    snprintf(fmt, sizeof(fmt), "\"%%s\":%%.%df", precision);
    snprintf(field, sizeof(field), fmt, key, value);
    builder_append(builder, field);
}

/*
 * Add a boolean field to the JSON object.
 */
void ws_json_builder_add_bool(ws_json_builder_t *builder, const char *key, bool value) {
    char field[256];
    
    if (!builder || !key) return;
    
    builder_add_comma(builder);
    snprintf(field, sizeof(field), "\"%s\":%s", key, value ? "true" : "false");
    builder_append(builder, field);
}

/*
 * Add a null field to the JSON object.
 */
void ws_json_builder_add_null(ws_json_builder_t *builder, const char *key) {
    char field[256];
    
    if (!builder || !key) return;
    
    builder_add_comma(builder);
    snprintf(field, sizeof(field), "\"%s\":null", key);
    builder_append(builder, field);
}

/*
 * Get the final JSON string.
 */
const char *ws_json_builder_get(ws_json_builder_t *builder) {
    if (!builder || builder->error) return NULL;
    return builder->buffer;
}

/* ============================================================================
 * JSON Array Builder Functions
 * ============================================================================ */

/*
 * Append to array builder buffer with overflow checking.
 */
static void array_append(ws_json_array_builder_t *builder, const char *str) {
    size_t str_len;
    
    if (!builder || builder->error || !str) return;
    
    str_len = strlen(str);
    if (builder->length + str_len >= builder->capacity) {
        builder->error = 1;
        return;
    }
    
    memcpy(builder->buffer + builder->length, str, str_len + 1);
    builder->length += str_len;
}

/*
 * Initialize a JSON array builder.
 */
void ws_json_array_init(ws_json_array_builder_t *builder, char *buffer, size_t capacity) {
    if (!builder || !buffer || capacity == 0) return;
    
    builder->buffer = buffer;
    builder->capacity = capacity;
    builder->length = 0;
    builder->item_count = 0;
    builder->error = 0;
    
    /* Start with opening bracket */
    buffer[0] = '[';
    buffer[1] = '\0';
    builder->length = 1;
}

/*
 * Add an item to the JSON array.
 */
void ws_json_array_add(ws_json_array_builder_t *builder, const char *item) {
    if (!builder || !item) return;
    
    if (builder->item_count > 0) {
        array_append(builder, ",");
    }
    array_append(builder, item);
    builder->item_count++;
}

/*
 * Finalize the JSON array.
 */
void ws_json_array_end(ws_json_array_builder_t *builder) {
    if (!builder) return;
    array_append(builder, "]");
}

/*
 * Get the final JSON array string.
 */
const char *ws_json_array_get(ws_json_array_builder_t *builder) {
    if (!builder || builder->error) return NULL;
    return builder->buffer;
}

/* ============================================================================
 * Sensor JSON Helper Functions
 * ============================================================================ */

/*
 * Format a Unix timestamp as a string.
 */
void ws_format_timestamp(char *buffer, size_t bufsize, time_t timestamp) {
    if (!buffer || bufsize == 0) return;
    snprintf(buffer, bufsize, "%ld", (long)timestamp);
}

/*
 * Build base sensor JSON from sc-prototype template.
 */
int ws_build_sensor_json_base(char *output, size_t output_len,
                               const char *sensor, const char *measures, const char *unit,
                               const char *sensor_id, const char *sensor_name,
                               bool internal, time_t timestamp) {
    const char *prototype;
    
    if (!output || output_len == 0) return -1;
    
    prototype = ws_get_prototype_cached();
    if (!prototype || !*prototype) {
        output[0] = '\0';
        return -1;
    }
    
    /* Start with a copy of the prototype */
    strncpy(output, prototype, output_len - 1);
    output[output_len - 1] = '\0';
    
    /* Replace common fields */
    ws_json_replace_null_string(output, "sensor", sensor);
    ws_json_replace_null_string(output, "measures", measures);
    ws_json_replace_null_string(output, "unit", unit);
    ws_json_replace_null_string(output, "sensor_id", sensor_id);
    
    /* Only set sensor_name if provided */
    if (sensor_name && sensor_name[0] != '\0') {
        ws_json_replace_null_string(output, "sensor_name", sensor_name);
    }
    
    ws_json_replace_null_bool(output, "internal", internal);
    
    /* Add timestamp as integer */
    ws_json_replace_null_int(output, "timestamp", (long)timestamp);
    
    return 0;
}

/*
 * Add value field to sensor JSON.
 */
void ws_sensor_json_set_value(char *json, double value, int precision) {
    char value_str[64];
    char fmt[16];
    
    if (!json) return;
    
    snprintf(fmt, sizeof(fmt), "%%.%df", precision);
    snprintf(value_str, sizeof(value_str), fmt, value);
    
    /* Replace "value":null with "value":X.XX */
    char search[] = "\"value\":null";
    char replace[128];
    snprintf(replace, sizeof(replace), "\"value\":%s", value_str);
    
    char *pos = strstr(json, search);
    if (pos) {
        size_t search_len = strlen(search);
        size_t replace_len = strlen(replace);
        size_t tail_len = strlen(pos + search_len);
        memmove(pos + replace_len, pos + search_len, tail_len + 1);
        memcpy(pos, replace, replace_len);
    }
}

/*
 * Add error field to sensor JSON and set value to null.
 */
void ws_sensor_json_set_error(char *json, const char *error_msg) {
    char escaped[256];
    
    if (!json) return;
    
    /* Value stays as null (or we explicitly set it) */
    /* Replace error:null with error:"message" */
    if (error_msg) {
        ws_json_escape_string(error_msg, escaped, sizeof(escaped));
        ws_json_replace_null_string(json, "error", escaped);
    }
}

/*
 * Set the config field in sensor JSON to a custom JSON object.
 */
void ws_sensor_json_set_config(char *json, size_t json_capacity, const char *config_json) {
    char *pos;
    const char *search = "\"config\":null";
    size_t search_len, config_len, tail_len;
    
    if (!json || !config_json) return;
    
    pos = strstr(json, search);
    if (!pos) return;
    
    search_len = strlen(search);
    config_len = strlen(config_json);
    tail_len = strlen(pos + search_len);
    
    /* Check if we have enough space */
    /* We're replacing "config":null with "config":<config_json> */
    /* The "config": part stays (9 chars), null (4 chars) gets replaced */
    size_t new_total = (pos - json) + 9 + config_len + tail_len + 1;
    if (new_total > json_capacity) return;
    
    /* Move tail to make room */
    memmove(pos + 9 + config_len, pos + search_len, tail_len + 1);
    /* Copy config JSON */
    memcpy(pos + 9, config_json, config_len);
}

/*
 * Build a config JSON object with common fields.
 */
int ws_build_config_base(char *buffer, size_t bufsize, const char *version) {
    if (!buffer || bufsize == 0) return 0;
    
    if (version) {
        return snprintf(buffer, bufsize, "{\"software_version\":\"%s\"", version);
    } else {
        return snprintf(buffer, bufsize, "{");
    }
}

/*
 * Append a string field to a config JSON object.
 */
int ws_config_add_string(char *buffer, size_t bufsize, const char *key, const char *value) {
    size_t current_len;
    char append[256];
    int append_len;
    
    if (!buffer || !key || !value) return 0;
    
    current_len = strlen(buffer);
    if (current_len >= bufsize - 1) return 0;
    
    append_len = snprintf(append, sizeof(append), ",\"%s\":\"%s\"", key, value);
    if (current_len + append_len >= bufsize) return 0;
    
    strcat(buffer, append);
    return append_len;
}

/*
 * Append an integer field to a config JSON object.
 */
int ws_config_add_int(char *buffer, size_t bufsize, const char *key, long value) {
    size_t current_len;
    char append[128];
    int append_len;
    
    if (!buffer || !key) return 0;
    
    current_len = strlen(buffer);
    if (current_len >= bufsize - 1) return 0;
    
    append_len = snprintf(append, sizeof(append), ",\"%s\":%ld", key, value);
    if (current_len + append_len >= bufsize) return 0;
    
    strcat(buffer, append);
    return append_len;
}

/*
 * Append a nested JSON object to a config JSON object.
 */
int ws_config_add_object(char *buffer, size_t bufsize, const char *key, const char *object_json) {
    size_t current_len;
    char append[1024];
    int append_len;
    
    if (!buffer || !key || !object_json) return 0;
    
    current_len = strlen(buffer);
    if (current_len >= bufsize - 1) return 0;
    
    append_len = snprintf(append, sizeof(append), ",\"%s\":%s", key, object_json);
    if (current_len + append_len >= bufsize) return 0;
    
    strcat(buffer, append);
    return append_len;
}

/*
 * Close a config JSON object.
 */
void ws_config_end(char *buffer) {
    size_t len;
    
    if (!buffer) return;
    
    len = strlen(buffer);
    if (len > 0) {
        strcat(buffer, "}");
    }
}
