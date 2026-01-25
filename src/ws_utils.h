/*
 * ws_utils.h - WildlifeSystems shared sensor utilities
 * Copyright (C) 2024 Wildlife Systems
 *
 * Common utilities for WildlifeSystems sensor drivers.
 * This code is shared between sensor-dht11, sensor-w1therm, and other sensor drivers.
 *
 * Functions provided:
 *   - JSON string escaping and field replacement
 *   - sc-prototype interaction for JSON templates
 *   - Common command handling (identify, list)
 */

#ifndef WS_UTILS_H
#define WS_UTILS_H

#include <stddef.h>
#include <stdbool.h>
#include <time.h>

/* Common exit codes for WildlifeSystems sensors */
#define WS_EXIT_SUCCESS     0
#define WS_EXIT_IDENTIFY    60
#define WS_EXIT_INVALID_ARG 20

/* Location filter values for internal/external sensor filtering */
typedef enum {
    WS_LOCATION_ALL = 0,
    WS_LOCATION_INTERNAL,
    WS_LOCATION_EXTERNAL
} ws_location_filter_t;

/* Maximum size for JSON buffers */
#define WS_MAX_JSON_SIZE    8192

/*
 * Escape a string for safe inclusion in JSON output.
 * Escapes backslashes and double quotes.
 *
 * @param src      Source string to escape
 * @param dst      Destination buffer for escaped string
 * @param dst_len  Size of destination buffer
 */
void ws_json_escape_string(const char *src, char *dst, size_t dst_len);

/*
 * Replace a JSON null value with a string value.
 * e.g. "field":null -> "field":"value"
 *
 * @param json     JSON string to modify (in-place)
 * @param key      Field name to search for
 * @param value    String value to insert (without quotes)
 */
void ws_json_replace_null_string(char *json, const char *key, const char *value);

/*
 * Replace a JSON null value with a number value.
 * e.g. "value":null -> "value":23.456
 *
 * @param json     JSON string to modify (in-place)
 * @param key      Field name to search for
 * @param value    Numeric value to insert
 */
void ws_json_replace_null_number(char *json, const char *key, double value);

/*
 * Replace a JSON null value with an integer value.
 * e.g. "timestamp":null -> "timestamp":1234567890
 *
 * @param json     JSON string to modify (in-place)
 * @param key      Field name to search for
 * @param value    Integer value to insert
 */
void ws_json_replace_null_int(char *json, const char *key, long value);

/*
 * Replace a JSON null value with a boolean value.
 * e.g. "internal":null -> "internal":true
 *
 * @param json     JSON string to modify (in-place)
 * @param key      Field name to search for
 * @param value    Boolean value to insert
 */
void ws_json_replace_null_bool(char *json, const char *key, bool value);

/*
 * Get JSON prototype template by calling sc-prototype.
 * Returns dynamically allocated string, caller must free.
 *
 * @return         Allocated string with prototype, or NULL on error
 */
char *ws_get_sc_prototype(void);

/*
 * Get cached JSON prototype template.
 * Returns pointer to internal buffer. Calls sc-prototype on first use.
 *
 * @return         Pointer to cached prototype, or NULL on error
 */
const char *ws_get_prototype_cached(void);

/*
 * Get current Unix timestamp.
 *
 * @return         Current time as Unix timestamp
 */
time_t ws_get_timestamp(void);

/*
 * Handle the 'identify' command.
 * Exits with WS_EXIT_IDENTIFY code.
 */
void ws_cmd_identify(void);

/*
 * Handle the 'list' command for sensors with single measurement type.
 * Prints the measurement type and exits.
 *
 * @param measurement  The measurement type to print (e.g., "temperature")
 */
void ws_cmd_list_single(const char *measurement);

/*
 * Handle the 'list' command for sensors with multiple measurement types.
 * Prints each type on a separate line and exits.
 *
 * @param measurements  NULL-terminated array of measurement type strings
 */
void ws_cmd_list_multiple(const char **measurements);

/*
 * Get Raspberry Pi serial number from /proc/cpuinfo.
 * Returns just the raw serial number without any suffix.
 * Returns dynamically allocated string, caller must free.
 *
 * @return         Serial number string, or NULL on failure
 */
char *ws_get_serial_number(void);

/*
 * Validate GPIO pin is in valid range for Raspberry Pi (2-27).
 *
 * @param pin      GPIO pin number to validate
 * @return         true if valid, false otherwise
 */
bool ws_validate_gpio_pin(int pin);

/*
 * Print version information for a sensor program.
 *
 * @param program_name  Name of the program (e.g., "sensor-dht11")
 * @param version       Version string
 */
void ws_print_version(const char *program_name, const char *version);

#endif /* WS_UTILS_H */
