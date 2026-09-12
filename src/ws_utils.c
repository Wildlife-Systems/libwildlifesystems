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
void ws_json_replace_null_string(char *json, size_t json_capacity,
                                 const char *key, const char *value) {
    char search[128];
    char *pos;
    size_t search_len, prefix_len, value_len, tail_len, new_total;

    if (!json || !key || !value) return;

    snprintf(search, sizeof(search), "\"%s\":null", key);
    pos = strstr(json, search);
    if (pos == NULL) {
        return;
    }

    search_len = strlen(search);
    /* The "<key>": part stays put; only null is replaced, by "<value>". */
    prefix_len = search_len - 4;
    value_len = strlen(value);
    tail_len = strlen(pos + search_len);

    /* prefix + opening quote + value + closing quote + tail + terminator.
       Refuse rather than truncate: a clipped value would leave the string
       unterminated and so invalidate the whole document, not just this key. */
    new_total = (size_t)(pos - json) + prefix_len + 1 + value_len + 1 + tail_len + 1;
    if (new_total > json_capacity) return;

    /* Shift the tail clear, then write the quoted value over the null. */
    memmove(pos + prefix_len + 1 + value_len + 1, pos + search_len, tail_len + 1);
    pos[prefix_len] = '"';
    memcpy(pos + prefix_len + 1, value, value_len);
    pos[prefix_len + 1 + value_len] = '"';
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
    /* sc-prototype ships "internal":false rather than null, so an existing
       boolean literal has to be accepted as well as null -- otherwise the
       field silently keeps the template's value. */
    static const char *const literals[] = { "null", "false", "true" };
    char search[128];
    char replace[256];
    char *pos = NULL;
    size_t search_len = 0, replace_len, tail_len;
    size_t i;

    if (!json || !key) return;

    for (i = 0; i < sizeof(literals) / sizeof(literals[0]); i++) {
        snprintf(search, sizeof(search), "\"%s\":%s", key, literals[i]);
        pos = strstr(json, search);
        if (pos) {
            search_len = strlen(search);
            break;
        }
    }
    if (pos == NULL) {
        return;
    }

    snprintf(replace, sizeof(replace), "\"%s\":%s", key, value ? "true" : "false");
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
 * Advance past a JSON string literal.
 * `ptr` must point at the opening quote. Returns a pointer to the character
 * after the closing quote, or to the terminating null if unterminated.
 */
static const char *ws_json_skip_string(const char *ptr) {
    ptr++;  /* opening quote */
    while (*ptr) {
        if (*ptr == '\\') {
            /* Escape: skip the backslash and whatever it escapes, taking care
               not to run off the end of a buffer ending in a lone backslash. */
            if (!*(ptr + 1)) return ptr + 1;
            ptr += 2;
            continue;
        }
        if (*ptr == '"') return ptr + 1;
        ptr++;
    }
    return ptr;
}

/*
 * Count top-level JSON objects in a buffer.
 */
int ws_json_count_objects(const char *buffer) {
    int count = 0;
    int depth = 0;
    const char *ptr = buffer;

    if (!buffer) return 0;

    while (*ptr) {
        if (*ptr == '"') {
            ptr = ws_json_skip_string(ptr);
            continue;
        }
        if (*ptr == '{') {
            if (depth == 0) count++;
            depth++;
        } else if (*ptr == '}') {
            if (depth > 0) depth--;
        }
        ptr++;
    }
    return count;
}

/*
 * Find the '}' matching the '{' at the start of an object.
 */
const char *ws_json_object_end(const char *start) {
    int depth = 0;
    const char *ptr = start;

    if (!start) return NULL;

    while (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r') ptr++;
    if (*ptr != '{') return NULL;

    while (*ptr) {
        if (*ptr == '"') {
            ptr = ws_json_skip_string(ptr);
            continue;
        }
        if (*ptr == '{') {
            depth++;
        } else if (*ptr == '}') {
            depth--;
            if (depth == 0) return ptr;
        }
        ptr++;
    }
    return NULL;  /* unterminated */
}

/*
 * Parse a nested JSON object field from a JSON object.
 */
char *ws_json_parse_object(const char *ptr, const char *end, const char *field) {
    char search[128];
    const char *field_ptr;
    const char *colon;
    const char *obj_start;
    const char *obj_end;

    if (!ptr || !end || !field) return NULL;

    snprintf(search, sizeof(search), "\"%s\"", field);
    field_ptr = strstr(ptr, search);

    if (!field_ptr || field_ptr >= end) return NULL;

    colon = strchr(field_ptr, ':');
    if (!colon || colon >= end) return NULL;

    obj_start = colon + 1;
    while (*obj_start == ' ' || *obj_start == '\t' ||
           *obj_start == '\n' || *obj_start == '\r') obj_start++;

    if (*obj_start != '{') return NULL;  /* not an object */

    obj_end = ws_json_object_end(obj_start);
    if (!obj_end) return NULL;

    return strndup(obj_start, (size_t)(obj_end - obj_start) + 1);
}

/*
 * Parse a JSON string field from a JSON object.
 */
char *ws_json_parse_string(const char *ptr, const char *end, const char *field) {
    char search[128];
    const char *field_ptr;
    const char *colon;
    const char *value;
    const char *value_end;

    if (!ptr || !end || !field) return NULL;

    snprintf(search, sizeof(search), "\"%s\"", field);
    field_ptr = strstr(ptr, search);

    if (!field_ptr || field_ptr >= end) return NULL;

    colon = strchr(field_ptr, ':');
    if (!colon || colon >= end) return NULL;

    /* The value must actually be a string: the first non-whitespace character
       after the colon has to be a quote. Without this check an object- or
       number-valued field returns whatever text happens to be quoted next -
       for "location":{"latitude":51.5} that is the nested key "latitude". */
    value = colon + 1;
    while (value < end && (*value == ' '  || *value == '\t' ||
                           *value == '\n' || *value == '\r')) {
        value++;
    }
    if (value >= end || *value != '"') return NULL;

    /* Closing quote, honouring \" escapes so a value containing an escaped
       quote is not truncated at it. */
    value_end = value + 1;
    while (value_end < end) {
        if (*value_end == '\\') {
            value_end += 2;  /* the escape and the character it escapes */
            continue;
        }
        if (*value_end == '"') break;
        value_end++;
    }
    if (value_end >= end || *value_end != '"') return NULL;  /* unterminated */

    return strndup(value + 1, (size_t)(value_end - value - 1));
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
 * Parse a JSON floating-point field from a JSON object.
 */
double ws_json_parse_double(const char *ptr, const char *end, const char *field, double default_val) {
    char search[128];
    const char *field_ptr;
    const char *colon;
    char *value_end;
    double value;

    if (!ptr || !end || !field) return default_val;

    snprintf(search, sizeof(search), "\"%s\"", field);
    field_ptr = strstr(ptr, search);

    if (!field_ptr || field_ptr >= end) return default_val;

    colon = strchr(field_ptr, ':');
    if (!colon || colon >= end) return default_val;

    colon++;
    while (*colon == ' ' || *colon == '\t') colon++;

    if (colon >= end) return default_val;

    value = strtod(colon, &value_end);

    /* No conversion performed - the field was present but not a number */
    if (value_end == colon) return default_val;

    return value;
}

/* ============================================================================
 * Node Location
 * ============================================================================ */

/*
 * Read the node's location from the GeoClue static location file.
 */
int ws_read_geolocation(ws_geolocation_t *out) {
    const char *path;
    char *buffer;
    char *line;
    char *saveptr = NULL;
    double values[4];
    int count = 0;

    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    path = getenv("WS_GEOLOCATION_FILE");
    if (!path || !*path) path = WS_GEOLOCATION_FILE_DEFAULT;

    buffer = ws_read_file(path, NULL);
    if (!buffer) {
        /* A node that has not been surveyed is a normal state, not a failure. */
        return 0;
    }

    for (line = strtok_r(buffer, "\n", &saveptr);
         line && count < 4;
         line = strtok_r(NULL, "\n", &saveptr)) {
        char *hash;
        char *start;
        char *end;
        char *num_end;
        double v;

        /* Strip an inline comment, then surrounding whitespace and any CR. */
        hash = strchr(line, '#');
        if (hash) *hash = '\0';

        start = line;
        while (*start == ' ' || *start == '\t' || *start == '\r') start++;
        end = start + strlen(start);
        while (end > start &&
               (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
        *end = '\0';

        if (*start == '\0') continue;  /* blank or comment-only */

        v = strtod(start, &num_end);
        if (num_end == start) {
            ws_log_warning("Ignoring unparseable line in %s: \"%s\"", path, start);
            continue;
        }
        values[count++] = v;
    }

    free(buffer);

    /* Latitude and longitude together are the minimum useful location. */
    if (count < 2) {
        if (count > 0) {
            ws_log_warning("%s has %d value(s); latitude and longitude are both "
                           "required, ignoring node location", path, count);
        }
        return 0;
    }

    if (values[0] < -90.0 || values[0] > 90.0) {
        ws_log_warning("Node latitude %f out of range [-90, 90] in %s; "
                       "ignoring node location", values[0], path);
        return 0;
    }
    if (values[1] < -180.0 || values[1] > 180.0) {
        ws_log_warning("Node longitude %f out of range [-180, 180] in %s; "
                       "ignoring node location", values[1], path);
        return 0;
    }
    if (count >= 4 && values[3] < 0.0) {
        ws_log_warning("Node location accuracy %f is negative in %s; "
                       "ignoring node location", values[3], path);
        return 0;
    }

    out->latitude  = values[0];
    out->longitude = values[1];
    if (count >= 3) {
        out->altitude = values[2];
        out->has_altitude = true;
    }
    if (count >= 4) {
        out->accuracy = values[3];
        out->has_accuracy = true;
    }
    out->valid = true;
    return 0;
}

/*
 * Render a node location as a GeoJSON Point.
 */
char *ws_geolocation_geojson(const ws_geolocation_t *g) {
    char buf[256];

    if (!g || !g->valid) return NULL;

    /* GeoJSON is [longitude, latitude], in that order. */
    if (g->has_altitude) {
        snprintf(buf, sizeof(buf),
                 "{\"type\":\"Point\",\"coordinates\":[%.6f,%.6f,%.2f]}",
                 g->longitude, g->latitude, g->altitude);
    } else {
        snprintf(buf, sizeof(buf),
                 "{\"type\":\"Point\",\"coordinates\":[%.6f,%.6f]}",
                 g->longitude, g->latitude);
    }
    return strdup(buf);
}

/* ============================================================================
 * Sensor Location
 * ============================================================================ */

/* Sentinel for "this numeric field was not present". No legitimate latitude,
   longitude, altitude or accuracy can take this value. */
#define WS_LOC_ABSENT (-1e300)

/*
 * Parse the "location" field of one sensor config object.
 */
int ws_parse_sensor_location(const char *ptr, const char *end, ws_location_t *out) {
    char *obj;
    char *token;

    if (!out) return -1;

    memset(out, 0, sizeof(*out));
    out->source = WS_LOC_UNDECLARED;

    if (!ptr || !end) return -1;

    /* Object form first. ws_json_parse_string() rejects non-string values, so
       the order is not load-bearing, but trying the richer form first keeps
       the intent obvious. */
    obj = ws_json_parse_object(ptr, end, "location");
    if (obj) {
        const char *oe = obj + strlen(obj);
        double lat = ws_json_parse_double(obj, oe, "latitude",  WS_LOC_ABSENT);
        double lon = ws_json_parse_double(obj, oe, "longitude", WS_LOC_ABSENT);
        double alt = ws_json_parse_double(obj, oe, "altitude",  WS_LOC_ABSENT);
        double acc = ws_json_parse_double(obj, oe, "accuracy",  WS_LOC_ABSENT);

        if (lat == WS_LOC_ABSENT || lon == WS_LOC_ABSENT) {
            ws_log_warning("Sensor location needs both latitude and longitude; "
                           "ignoring location");
        } else if (lat < -90.0 || lat > 90.0) {
            ws_log_warning("Sensor latitude %f out of range [-90, 90]; "
                           "ignoring location", lat);
        } else if (lon < -180.0 || lon > 180.0) {
            ws_log_warning("Sensor longitude %f out of range [-180, 180]; "
                           "ignoring location", lon);
        } else if (acc != WS_LOC_ABSENT && acc < 0.0) {
            ws_log_warning("Sensor location accuracy %f is negative; "
                           "ignoring location", acc);
        } else {
            out->source    = WS_LOC_EXPLICIT;
            out->latitude  = lat;
            out->longitude = lon;
            if (alt != WS_LOC_ABSENT) {
                out->altitude = alt;
                out->has_altitude = true;
            }
            if (acc != WS_LOC_ABSENT) {
                out->accuracy = acc;
                out->has_accuracy = true;
            }
        }
        free(obj);
        return 0;
    }

    /* Token form. */
    token = ws_json_parse_string(ptr, end, "location");
    if (token) {
        if (strcmp(token, "{{node}}") == 0) {
            out->source = WS_LOC_NODE;
        } else if (strcmp(token, "{{none}}") == 0) {
            out->source = WS_LOC_NONE;
        } else {
            ws_log_warning("Unrecognised sensor location \"%s\"; expected "
                           "\"{{node}}\", \"{{none}}\" or coordinates", token);
        }
        free(token);
    }

    /* No "location" key at all leaves WS_LOC_UNDECLARED. Logged so that a
       sensor with no declared position is distinguishable from one whose
       config was never reached - syslog only, because this fires on every
       read cycle for every unconfigured sensor and would otherwise fill the
       per-driver stderr log. */
    if (out->source == WS_LOC_UNDECLARED && !token) {
        ws_log_info("No location declared for this sensor; publishing none");
    }
    return 0;
}

/*
 * Render a location as the JSON value for a reading's "location" field.
 */
char *ws_location_json(const ws_location_t *loc) {
    char buf[256];

    if (!loc) return NULL;

    switch (loc->source) {
        case WS_LOC_NODE: {
            /* Resolved here rather than downstream, so a reading carries the
               position it was actually taken at. A node that is moved and
               re-surveyed does not retrospectively relocate its own history.
               Falls back to the unresolved token when the node has no usable
               location, which keeps the declaration rather than dropping it. */
            ws_geolocation_t node;
            char *json;

            ws_read_geolocation(&node);
            json = ws_geolocation_geojson(&node);
            if (json) return json;
            return strdup("\"{{node}}\"");
        }

        case WS_LOC_NONE:
            return strdup("\"{{none}}\"");

        case WS_LOC_EXPLICIT:
            /* GeoJSON is [longitude, latitude], in that order. */
            if (loc->has_altitude) {
                snprintf(buf, sizeof(buf),
                         "{\"type\":\"Point\",\"coordinates\":[%.6f,%.6f,%.2f]}",
                         loc->longitude, loc->latitude, loc->altitude);
            } else {
                snprintf(buf, sizeof(buf),
                         "{\"type\":\"Point\",\"coordinates\":[%.6f,%.6f]}",
                         loc->longitude, loc->latitude);
            }
            return strdup(buf);

        case WS_LOC_UNDECLARED:
        default:
            return NULL;
    }
}

/* ============================================================================
 * Sensor Configuration
 * ============================================================================ */

int ws_config_iter_open(ws_config_iter_t *it, const char *path) {
    if (!it) return -1;

    it->buffer = NULL;
    it->ptr = NULL;
    it->count = 0;
    it->index = 0;

    if (!path) return -1;

    /* A driver with no config file is the normal single-sensor case, not an
       error: the caller falls back to its own defaults. */
    it->buffer = ws_read_file(path, NULL);
    if (!it->buffer) return 0;

    it->count = ws_json_count_objects(it->buffer);
    if (it->count <= 0) {
        it->count = 0;
        return 0;
    }

    it->ptr = it->buffer;
    return it->count;
}

bool ws_config_iter_next(ws_config_iter_t *it, ws_sensor_config_base_t *base,
                         const char **start, const char **end) {
    const char *obj_start;
    const char *obj_end;

    if (!it || !base || !start || !end) return false;
    if (!it->ptr || it->index >= it->count) return false;

    obj_start = strchr(it->ptr, '{');
    if (!obj_start) return false;

    /* Brace-matched, so an entry containing a nested object - a "location"
       with coordinates, say - is bounded correctly. */
    obj_end = ws_json_object_end(obj_start);
    if (!obj_end) return false;

    memset(base, 0, sizeof(*base));
    base->internal    = ws_json_parse_bool(obj_start, obj_end, "internal", false);
    base->sensor_id   = ws_json_parse_string(obj_start, obj_end, "sensor_id");
    base->sensor_name = ws_json_parse_string(obj_start, obj_end, "sensor_name");
    ws_parse_sensor_location(obj_start, obj_end, &base->location);

    *start = obj_start;
    *end = obj_end;

    it->ptr = obj_end + 1;
    it->index++;
    return true;
}

void ws_config_iter_close(ws_config_iter_t *it) {
    if (!it) return;
    free(it->buffer);
    it->buffer = NULL;
    it->ptr = NULL;
    it->count = 0;
    it->index = 0;
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
                               const char *sensor, const char *device,
                               const char *measures, const char *unit,
                               const char *sensor_id, const char *sensor_name,
                               bool internal, const ws_location_t *location,
                               time_t timestamp) {
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
    ws_json_replace_null_string(output, output_len, "sensor", sensor);
    ws_json_replace_null_string(output, output_len, "measures", measures);
    ws_json_replace_null_string(output, output_len, "unit", unit);
    ws_json_replace_null_string(output, output_len, "sensor_id", sensor_id);

    /* Physical device model, for the STA Sensor entity. Optional: a driver
       that cannot name its device leaves the field null. */
    if (device && device[0] != '\0') {
        ws_json_replace_null_string(output, output_len, "device", device);
    }

    /* Location is a token string or a GeoJSON object, so it goes in raw.
       An undeclared location leaves the field null. */
    if (location) {
        char *location_json = ws_location_json(location);
        if (location_json) {
            ws_json_replace_null_raw(output, output_len, "location", location_json);
            free(location_json);
        }
    }
    
    /* Only set sensor_name if provided */
    if (sensor_name && sensor_name[0] != '\0') {
        ws_json_replace_null_string(output, output_len, "sensor_name", sensor_name);
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
void ws_sensor_json_set_error(char *json, size_t json_capacity,
                              const char *error_msg) {
    char escaped[256];
    
    if (!json) return;
    
    /* Value stays as null (or we explicitly set it) */
    /* Replace error:null with error:"message" */
    if (error_msg) {
        ws_json_escape_string(error_msg, escaped, sizeof(escaped));
        ws_json_replace_null_string(json, json_capacity, "error", escaped);
    }
}

/*
 * Set a reading's outcome: a value or an error, never both.
 */
void ws_sensor_json_set_result(char *json, size_t json_capacity, double value,
                               int precision, const char *error_msg) {
    if (!json) return;

    if (error_msg && error_msg[0] != '\0') {
        ws_sensor_json_set_error(json, json_capacity, error_msg);
    } else {
        ws_sensor_json_set_value(json, value, precision);
    }
}

/*
 * Set the config field in sensor JSON to a custom JSON object.
 */
void ws_json_replace_null_raw(char *json, size_t json_capacity,
                              const char *key, const char *raw_json) {
    char search[128];
    char *pos;
    size_t search_len, prefix_len, raw_len, tail_len, new_total;

    if (!json || !key || !raw_json) return;

    snprintf(search, sizeof(search), "\"%s\":null", key);
    pos = strstr(json, search);
    if (!pos) return;

    search_len = strlen(search);
    /* The "<key>": part stays put; only the four characters of null go. */
    prefix_len = search_len - 4;
    raw_len = strlen(raw_json);
    tail_len = strlen(pos + search_len);

    new_total = (size_t)(pos - json) + prefix_len + raw_len + tail_len + 1;
    if (new_total > json_capacity) return;

    memmove(pos + prefix_len + raw_len, pos + search_len, tail_len + 1);
    memcpy(pos + prefix_len, raw_json, raw_len);
}

void ws_sensor_json_set_config(char *json, size_t json_capacity, const char *config_json) {
    ws_json_replace_null_raw(json, json_capacity, "config", config_json);
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
