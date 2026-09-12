/*
 * ws_utils.h - WildlifeSystems shared sensor utilities
 * Copyright (C) 2024 Wildlife Systems
 *
 * Common utilities for WildlifeSystems sensor drivers.
 * This code is shared between sensor-dht11, sensor-w1therm, and other sensor drivers.
 *
 * Functions provided:
 *   - JSON string escaping and field replacement
 *   - JSON output building
 *   - sc-prototype interaction for JSON templates
 *   - Common command handling (identify, list)
 *   - Unified logging (stderr + syslog)
 */

#ifndef WS_UTILS_H
#define WS_UTILS_H

#include <stddef.h>
#include <stdbool.h>
#include <time.h>
#include <stdarg.h>

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

/* ============================================================================
 * Units of measurement
 * ============================================================================
 * A unit is part of a Datastream's identity downstream, so a misspelling does
 * not fail: it silently creates a second Datastream for the same quantity and
 * splits the series. That has happened twice in this project ("percant", and
 * "percent" where others said "percentage"), so the spellings live here rather
 * than as a literal at each call site.
 */
#define WS_UNIT_CELSIUS    "Celsius"
#define WS_UNIT_PERCENTAGE "percentage"
#define WS_UNIT_HPA        "hPa"
#define WS_UNIT_OHMS       "Ohms"

/*
 * Map a unit name to its canonical spelling.
 *
 * Matching ignores case, so "celsius", "Celsius" and "CELSIUS" all give
 * WS_UNIT_CELSIUS. For callers outside C - a shell script passing a unit on a
 * command line - this turns a typo into a rejection rather than a second
 * Datastream.
 *
 * @param name  Unit name to look up
 * @return      Canonical spelling (a WS_UNIT_* constant), or NULL if the name
 *              is not a known unit
 */
const char *ws_unit_canonical(const char *name);

/* Config path macro - generates "/etc/ws/sensors/<name>.json" */
#define WS_CONFIG_PATH(name) "/etc/ws/sensors/" name ".json"

/*
 * Initialize syslog for a sensor program.
 * Should be called once at program startup.
 *
 * @param program_name  Name of the program for syslog ident
 */
void ws_log_init(const char *program_name);

/*
 * Log an error to both stderr and syslog.
 * Uses printf-style formatting.
 *
 * @param fmt       Format string (printf-style)
 * @param ...       Format arguments
 */
void ws_log_error(const char *fmt, ...);

/*
 * Log a warning to both stderr and syslog.
 *
 * @param fmt       Format string (printf-style)
 * @param ...       Format arguments
 */
void ws_log_warning(const char *fmt, ...);

/*
 * Log an info message to syslog only (not stderr).
 *
 * @param fmt       Format string (printf-style)
 * @param ...       Format arguments
 */
void ws_log_info(const char *fmt, ...);

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
 * The value is inserted verbatim, so a caller passing text that may contain a
 * quote or backslash must escape it first with ws_json_escape_string().
 *
 * Does nothing if the key is not present as null, or if the result would not
 * fit in json_capacity. A value too long to fit is refused outright rather
 * than clipped: a clipped value would leave the JSON string unterminated and
 * invalidate the whole document, not merely this one field.
 *
 * @param json          JSON buffer to modify (in-place)
 * @param json_capacity Size of the buffer
 * @param key           Field name to search for
 * @param value         String value to insert (without quotes)
 */
void ws_json_replace_null_string(char *json, size_t json_capacity,
                                 const char *key, const char *value);

/*
 * Replace a JSON null value with a number value.
 *
 * Takes no capacity: the inserted text is a formatted scalar of bounded
 * length, unlike the string and raw variants, which insert caller-supplied
 * text and so require the buffer size.
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
 * An existing boolean literal is also replaced, so a template that ships
 * "internal":false (as sc-prototype does) is updated rather than left alone.
 *
 * @param json     JSON string to modify (in-place)
 * @param key      Field name to search for
 * @param value    Boolean value to insert
 */
void ws_json_replace_null_bool(char *json, const char *key, bool value);

/*
 * Replace "key":null with "key":<raw_json>, inserting the value verbatim.
 *
 * For values that are not scalars - an object, an array, or a string that is
 * already quoted. Does nothing if the key is not present as null, or if the
 * result would not fit in json_capacity.
 *
 * @param json          JSON buffer to modify
 * @param json_capacity Size of the buffer
 * @param key           Field name (without quotes)
 * @param raw_json      Value to insert verbatim, including any quotes or braces
 */
void ws_json_replace_null_raw(char *json, size_t json_capacity,
                              const char *key, const char *raw_json);

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
 * Confirm the sc-prototype template is available, before any reading is taken.
 *
 * Every reading needs the template, so a driver should ask once, up front,
 * and fail with nothing printed if it is missing. Finding out per reading is
 * too late: the hardware has been read, and an array is half built. Two
 * drivers used to emit "[,]" in that state, with exit 0.
 *
 * Nothing on stdout means "this driver could not report", where "[]" would
 * mean "this node has no sensors"; the two must not be confused.
 *
 * @return  0 if the template is available; otherwise logs the failure and
 *          returns WS_EXIT_INVALID_ARG, so a driver can return it directly
 */
int ws_require_prototype(void);

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
 * Print a driver's usage line to stderr.
 *
 * The measurement names are the same NULL-terminated array given to
 * ws_cmd_list_multiple(), so usage cannot advertise a measurement the driver
 * will not accept, nor omit one it will. Hand-written usage strings had already
 * drifted from both.
 *
 * @param program       Program name, e.g. "sensor-dht11"
 * @param measurements  NULL-terminated measurement names, or NULL for none
 */
void ws_cmd_usage(const char *program, const char **measurements);

/*
 * Report an unrecognised argument, with the usage, and give the exit code.
 *
 * @param program       Program name
 * @param arg           The unrecognised argument
 * @param measurements  NULL-terminated measurement names, or NULL for none
 * @return              WS_EXIT_INVALID_ARG, so a driver can return this directly
 */
int ws_cmd_unknown_arg(const char *program, const char *arg,
                       const char **measurements);

/*
 * Is `arg` one of the driver's measurement names?
 *
 * Lets a driver accept exactly the measurements it lists, read from the same
 * array, so what it lists, what it accepts and what it prints in usage cannot
 * disagree.
 *
 * @param arg           Argument to test
 * @param measurements  NULL-terminated measurement names, or NULL for none
 * @return              true if arg names a measurement
 */
bool ws_arg_is_measurement(const char *arg, const char **measurements);

/*
 * One reading of a driver's "mock" output.
 *
 * Drivers keep their own plausible values: what a reading should look like is
 * the driver's business, and a table at the call site reads better than the
 * same values hidden behind library arguments.
 */
typedef struct {
    const char *sensor;     /* Sensor name, e.g. "bme680_temperature" */
    const char *measures;   /* Quantity measured, e.g. "temperature" */
    const char *id_suffix;  /* sensor_id suffix: NULL to use measures, "" for no
                               suffix at all. Needed because the gas reading
                               measures "resistance" but is identified as
                               "<id>_gas_resistance", while a single-measurement
                               driver suffixes nothing */
    const char *unit;       /* A WS_UNIT_* constant */
    double value;           /* The value to report */
    int precision;          /* Decimal places */
    const char *location;   /* "{{node}}", "{{none}}", or NULL for undeclared.
                               A real reading takes this from its config; a
                               mock declares it here, so a mock reading has the
                               same shape as a real one. sensor-onboard's mock
                               declares its physical sensors at the node */
} ws_mock_reading_t;

/*
 * Handle the 'mock' command: emit fixed readings in the real output format.
 *
 * The point of mock is to look exactly like a real read, so the readings are
 * built the same way, with sensor_id suffixed per measurement and the array
 * assembled by the library rather than by hand. All readings share a timestamp.
 *
 * A host with no Pi serial still produces usable output, falling back to
 * serial_suffix as the id: mock exists to work without the hardware.
 *
 * Prints nothing and fails if the template is unavailable, rather than emitting
 * an empty array that would read as "this node has no sensors". A reading
 * whose location token is not one of the two accepted is likewise an error: it
 * is a mistake in the driver's own table.
 *
 * @param device        Physical device model, e.g. "dht11", or NULL
 * @param serial_suffix Suffix for the Pi serial, e.g. "dht11_mock"
 * @param sensor_name   sensor_name for every reading, e.g. "Mock DHT11"
 * @param readings      The readings to emit
 * @param count         How many
 * @return              WS_EXIT_SUCCESS, or WS_EXIT_INVALID_ARG on failure
 */
int ws_cmd_mock(const char *device, const char *serial_suffix,
                const char *sensor_name,
                const ws_mock_reading_t *readings, size_t count);

/*
 * Get Raspberry Pi serial number from /proc/cpuinfo.
 * Returns just the raw serial number without any suffix, with surrounding
 * whitespace trimmed. Returns dynamically allocated string, caller must free.
 *
 * Must agree byte for byte with "pi-data serial", which reads the same line:
 * sr takes node_id from pi-data while the drivers build sensor_id from this,
 * so any disagreement would leave a reading whose sensor_id prefix does not
 * match its own node_id.
 *
 * $WS_CPUINFO_FILE overrides the path, for testing.
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

/*
 * Read entire file into malloc'd buffer.
 * Returns NULL on error. Caller must free the returned buffer.
 *
 * @param path      Path to file to read
 * @param size_out  If not NULL, receives the size of the file
 * @return          Allocated buffer with file contents (null-terminated), or NULL on error
 */
char *ws_read_file(const char *path, size_t *size_out);

/*
 * Count top-level JSON objects in a buffer.
 * Braces inside string literals are ignored, as are braces nested inside
 * another object, so a config array of objects returns one count per entry
 * however deeply those entries are structured.
 *
 * @param buffer    JSON buffer to scan
 * @return          Number of top-level objects found
 */
int ws_json_count_objects(const char *buffer);

/*
 * Find the '}' matching the '{' at the start of an object.
 * Respects nesting and skips string literals, including \" escapes, so it is
 * safe on objects containing nested objects or braces inside string values.
 * Use in place of strchr(ptr, '}') when bounding a config object.
 *
 * @param start     Pointer to the opening '{' (leading whitespace is skipped)
 * @return          Pointer to the matching '}', or NULL if unterminated or
 *                  if start does not point at an object
 */
const char *ws_json_object_end(const char *start);

/*
 * Parse a nested JSON object field from a JSON object.
 * Searches for "field":{...} between ptr and end, returning the object
 * including its surrounding braces.
 *
 * @param ptr       Start of JSON object to search
 * @param end       End of JSON object
 * @param field     Field name to search for (without quotes)
 * @return          Allocated string with the object, or NULL if not found or
 *                  malformed. Caller must free.
 */
char *ws_json_parse_object(const char *ptr, const char *end, const char *field);

/*
 * Parse a JSON string field from a JSON object.
 * Searches for "field":"value" between ptr and end.
 *
 * The value must be a string. A field whose value is an object, array, number,
 * boolean or null returns NULL rather than the next quoted text in the buffer,
 * so a polymorphic field can be told apart by trying ws_json_parse_object()
 * and this function in either order.
 *
 * Escaped quotes (\") inside the value do not terminate it. The value is
 * returned exactly as it appears in the JSON, still escaped.
 *
 * @param ptr       Start of JSON object to search
 * @param end       End of JSON object (usually the closing '}')
 * @param field     Field name to search for (without quotes)
 * @return          Allocated string with field value, or NULL if not found or
 *                  not string-valued. Caller must free.
 */
char *ws_json_parse_string(const char *ptr, const char *end, const char *field);

/*
 * Parse a JSON boolean field from a JSON object.
 * Searches for "field":true or "field":false between ptr and end.
 *
 * @param ptr         Start of JSON object to search
 * @param end         End of JSON object
 * @param field       Field name to search for
 * @param default_val Value to return if field not found
 * @return            Parsed boolean value, or default_val if not found
 */
bool ws_json_parse_bool(const char *ptr, const char *end, const char *field, bool default_val);

/*
 * Parse a JSON integer field from a JSON object.
 * Searches for "field":123 between ptr and end.
 *
 * @param ptr         Start of JSON object to search
 * @param end         End of JSON object
 * @param field       Field name to search for
 * @param default_val Value to return if field not found
 * @return            Parsed integer value, or default_val if not found
 */
int ws_json_parse_int(const char *ptr, const char *end, const char *field, int default_val);

/*
 * Parse a JSON floating-point field from a JSON object.
 * Searches for "field":1.23 between ptr and end. Accepts anything strtod
 * accepts, including exponent notation and a leading '-'.
 *
 * @param ptr         Start of JSON object to search
 * @param end         End of JSON object
 * @param field       Field name to search for
 * @param default_val Value to return if field not found or not a number
 * @return            Parsed value, or default_val if not found
 */
double ws_json_parse_double(const char *ptr, const char *end, const char *field, double default_val);

/*
 * Get serial number with suffix appended.
 * Returns string in format "SERIAL_suffix".
 *
 * @param suffix    Suffix to append after underscore (e.g., "dht11", "bme680")
 * @return          Allocated string, or NULL on error. Caller must free.
 */
char *ws_get_serial_with_suffix(const char *suffix);

/* ============================================================================
 * Node Location
 * ============================================================================
 * Where the node itself is, from /etc/geolocation. This is the static location
 * file read by GeoClue's static source, so the format is not ours to change:
 * four numeric values, one per line, in the order latitude, longitude,
 * altitude, accuracy. '#' starts a comment; blank lines are ignored.
 */

#define WS_GEOLOC_FILE_DEFAULT "/etc/geolocation"

typedef struct {
    double latitude;      /* WGS84 decimal degrees, + = north */
    double longitude;     /* WGS84 decimal degrees, + = east */
    double altitude;      /* metres; valid only if has_altitude */
    double accuracy;      /* metres, radius; valid only if has_accuracy */
    bool   has_altitude;
    bool   has_accuracy;
    bool   valid;         /* false if the file is absent or unusable */
} ws_geolocation_t;

/*
 * Read the node's location from $GEOLOC_FILE, default /etc/geolocation.
 *
 * Tolerates the real-world GeoClue file: inline '#' comments, leading and
 * trailing whitespace, CRLF line endings, and fewer than four values. Latitude
 * and longitude are required; altitude and accuracy are optional.
 *
 * A missing file is not an error - it leaves out->valid false, because a node
 * that has not been surveyed is a normal state, not a failure. Values that are
 * present but out of range are rejected loudly, since emitting a bad position
 * is worse than emitting none.
 *
 * @param out   Populated on return; zeroed first
 * @return      0 on success (including "file absent"), -1 on bad arguments
 */
int ws_read_geolocation(ws_geolocation_t *out);

/*
 * Render a node location as GeoJSON.
 *
 * A Feature wrapping a Point, carrying the accuracy radius in metres as
 * properties.accuracy: a Point has nowhere to put it, and a surveyed radius is
 * worth keeping, being the difference between a position known to a metre and
 * one known to fifty. Always a Feature, and properties.accuracy is null rather
 * than absent where none was surveyed, so every position has one shape.
 *
 * NOTE: GeoJSON coordinate order is [longitude, latitude, altitude].
 *
 * @param g     Location to render
 * @return      Allocated JSON, or NULL if g is NULL or not valid. Caller frees.
 */
char *ws_geolocation_geojson(const ws_geolocation_t *g);

/* ============================================================================
 * Sensor Location
 * ============================================================================
 * Where a sensor physically sits, from the "location" key of its entry in
 * /etc/ws/sensors/<driver>.json. A sensor is frequently not at the node: a
 * 1-wire probe on a cable, a buried soil probe, a sensor up a mast.
 *
 * Note this is unrelated to ws_location_filter_t, which selects internal or
 * external sensors and says nothing about position.
 */

/* Where a sensor's position came from. */
typedef enum {
    WS_LOC_UNDECLARED = 0,  /* no "location" key, or one that failed validation */
    WS_LOC_NODE,            /* "{{node}}"  - at the node's own location */
    WS_LOC_NONE,            /* "{{none}}"  - deliberately has no location */
    WS_LOC_EXPLICIT         /* coordinates given in the config */
} ws_location_source_t;

typedef struct {
    ws_location_source_t source;
    double latitude;      /* WGS84 decimal degrees, WS_LOC_EXPLICIT only */
    double longitude;     /* WGS84 decimal degrees, WS_LOC_EXPLICIT only */
    double altitude;      /* metres; valid only if has_altitude */
    double accuracy;      /* metres, radius; valid only if has_accuracy */
    bool   has_altitude;
    bool   has_accuracy;
} ws_location_t;

/*
 * Parse the "location" field of one sensor config object.
 *
 * Accepts exactly three forms, per the sensor config specification:
 *   "location": "{{node}}"                      -> WS_LOC_NODE
 *   "location": "{{none}}"                      -> WS_LOC_NONE
 *   "location": { "latitude": .., "longitude": .. [, "altitude", "accuracy"] }
 *                                               -> WS_LOC_EXPLICIT
 * A missing "location" key yields WS_LOC_UNDECLARED.
 *
 * Validation follows ws_read_geolocation(): latitude in [-90, 90], longitude
 * in [-180, 180], accuracy >= 0, and both latitude and longitude required. A
 * value that fails validation, or an unrecognised token, is logged and yields
 * WS_LOC_UNDECLARED rather than failing the call - a typo in a coordinate must
 * not stop a sensor reporting readings.
 *
 * @param ptr   Start of the sensor's config object
 * @param end   End of that object (from ws_json_object_end())
 * @param out   Populated on return; zeroed first
 * @return      0 on success, -1 on bad arguments
 */
int ws_parse_sensor_location(const char *ptr, const char *end, ws_location_t *out);

/*
 * Parse a location token into the library's location type.
 *
 * Accepts "{{node}}", "{{none}}", and NULL or "" for an undeclared location.
 * Only the tokens: explicit coordinates belong in a sensor's config file,
 * where ws_parse_sensor_location() reads them, and accepting them here would
 * put a second place to get coordinate order wrong. Used by ws-emit for its
 * --location option and by ws_cmd_mock() for a driver's mock table, so the
 * two cannot disagree about what a token means.
 *
 * @param token  The token, or NULL
 * @param out    Populated on return; zeroed first
 * @return       0 on success, -1 if out is NULL or the token is unrecognised
 *               (which is logged)
 */
int ws_location_from_token(const char *token, ws_location_t *out);

/*
 * Render a location as the JSON value for a reading's "location" field.
 *
 * WS_LOC_NODE       -> the node's own position as a GeoJSON Point, read from
 *                     /etc/geolocation. Falls back to the literal "{{node}}"
 *                     when the node has no usable location, so the token can
 *                     still be resolved downstream rather than being lost.
 * WS_LOC_NONE       -> "\"{{none}}\""
 * WS_LOC_EXPLICIT   -> GeoJSON: a Feature wrapping a Point whose coordinates
 *                      are [lon, lat] or [lon, lat, alt], with the accuracy
 *                      radius in metres as properties.accuracy, null where
 *                      none was surveyed
 * WS_LOC_UNDECLARED -> NULL, so the caller leaves the field null
 *
 * @param loc   Location to render
 * @return      Allocated JSON value, or NULL for WS_LOC_UNDECLARED. Caller frees.
 */
char *ws_location_json(const ws_location_t *loc);

/* ============================================================================
 * Sensor Configuration
 * ============================================================================
 * Every driver's /etc/ws/sensors/<driver>.json entry carries the same four
 * fields; only the hardware-addressing field differs (a GPIO pin, an I2C
 * address, a 1-Wire hardware id). Drivers embed this struct rather than
 * redeclaring those fields, so a field added here reaches every driver at once.
 */

/*
 * The fields common to every sensor's config entry.
 *
 * Embed as the first member of a driver's own config struct:
 *
 *     typedef struct {
 *         ws_sensor_config_base_t base;
 *         int pin;
 *     } sensor_config_t;
 */
typedef struct {
    bool internal;          /* true if an internal sensor */
    char *sensor_id;        /* Unique sensor identifier, or NULL if unset */
    char *sensor_name;      /* Human-readable name, or NULL if unset */
    ws_location_t location; /* Where the sensor physically sits */
} ws_sensor_config_base_t;

/*
 * Free a base sensor config's string fields and NULL them.
 * Does not free the struct itself.
 *
 * @param config    Config whose fields should be freed
 */
void ws_sensor_config_free_fields(ws_sensor_config_base_t *config);

/*
 * Iterator over the sensor entries in a driver's config file.
 *
 * Replaces the read-count-allocate-walk sequence each driver used to carry:
 *
 *     ws_config_iter_t it;
 *     int n = ws_config_iter_open(&it, CONFIG_PATH);
 *     if (n <= 0) return NULL;
 *     configs = calloc((size_t)n, sizeof(*configs));
 *     ...
 *     while (ws_config_iter_next(&it, &configs[i].base, &start, &end)) {
 *         configs[i].pin = ws_json_parse_int(start, end, "pin", DEFAULT_PIN);
 *         i++;
 *     }
 *     ws_config_iter_close(&it);
 *
 * Treat the fields as private.
 */
typedef struct {
    char *buffer;       /* File contents; owned by the iterator */
    const char *ptr;    /* Scan position within buffer */
    int count;          /* Entries the file was found to hold */
    int index;          /* Entries yielded so far */
} ws_config_iter_t;

/*
 * Open a config file and count the entries it holds.
 *
 * A missing or empty file is not an error: it yields 0, and the driver falls
 * back to its own defaults. Always pair with ws_config_iter_close().
 *
 * @param it    Iterator to initialise
 * @param path  Config file path, e.g. WS_CONFIG_PATH("dht11")
 * @return      Number of entries (0 if the file is absent, unreadable or holds
 *              none), or -1 on bad arguments
 */
int ws_config_iter_open(ws_config_iter_t *it, const char *path);

/*
 * Fill `base` from the next entry and hand back that entry's bounds.
 *
 * The common fields are parsed for you; `start` and `end` bound the entry so
 * the driver can read its own fields from it with ws_json_parse_*(). A field
 * absent from the entry leaves the corresponding member zeroed (NULL for the
 * strings, false for internal, WS_LOC_UNDECLARED for location) rather than
 * guessing a default, so a driver can tell unset from set.
 *
 * @param it     Iterator from ws_config_iter_open()
 * @param base   Populated from this entry; zeroed first
 * @param start  Receives a pointer to the entry's opening brace
 * @param end    Receives a pointer to its matching closing brace
 * @return       true while entries remain, false once exhausted
 */
bool ws_config_iter_next(ws_config_iter_t *it, ws_sensor_config_base_t *base,
                         const char **start, const char **end);

/*
 * Release the iterator's buffer. Safe to call on an iterator whose open
 * returned 0 or -1, and safe to call twice.
 *
 * @param it    Iterator to close
 */
void ws_config_iter_close(ws_config_iter_t *it);

/* ============================================================================
 * Raspberry Pi boot configuration
 * ============================================================================
 * Some sensors need a device-tree directive in the Pi boot config before the
 * kernel will talk to them at all: a 1-Wire bus needs an overlay, I2C needs a
 * dtparam. A driver's "enable" command is how that gets added.
 *
 * None of this is specific to one bus, so it lives here rather than in
 * whichever driver happened to need it first.
 */

#define WS_BOOT_CONFIG_DEFAULT "/boot/firmware/config.txt"
#define WS_BOOT_CONFIG_LEGACY  "/boot/config.txt"

/*
 * Locate the Pi boot config.
 *
 * Prefers /boot/firmware/config.txt, falling back to the pre-Bookworm
 * /boot/config.txt. $WS_BOOT_CONFIG_FILE overrides both, which is how the tests
 * exercise this without a Pi.
 *
 * @return  Path to an existing config, or NULL if neither is present
 */
const char *ws_boot_config_path(void);

/*
 * Is `directive` already present in the boot config?
 *
 * Leading whitespace is ignored and comments are skipped. Matching is a prefix
 * match from the start of the directive, so "dtoverlay=w1-gpio" matches
 * "dtoverlay=w1-gpio,gpiopin=17,pullup=1" and a driver can tell its bus is
 * enabled whatever parameters someone gave it.
 *
 * @param path       Boot config to read
 * @param directive  Directive, or the leading part of one, to look for
 * @return           1 if present, 0 if absent, -1 if the file cannot be read
 */
int ws_boot_config_has(const char *path, const char *directive);

/*
 * Append `directive` to the boot config, under [all].
 *
 * A directive following a model-specific section such as [pi4] applies only to
 * that model, so an [all] section is added first if the file has none.
 *
 * @param path       Boot config to append to
 * @param directive  Directive to add
 * @param comment    Comment to precede it with, or NULL for none
 * @return           0 on success, -1 if the file cannot be written
 */
int ws_boot_config_add(const char *path, const char *directive, const char *comment);

/*
 * Implement a driver's "enable" command.
 *
 * Finds the boot config, reports if the directive is already present, otherwise
 * adds it and says a reboot is needed. This is the whole of "enable" for a
 * driver whose hardware needs a boot directive.
 *
 * @param directive  Directive to ensure, e.g. "dtparam=i2c_arm=on"
 * @param match      Prefix identifying it, e.g. "dtparam=i2c_arm", or NULL to
 *                   match the whole directive
 * @param what       Name used in the messages, e.g. "I2C interface"
 * @param added_by   Driver name, for the comment left in the file
 * @return           WS_EXIT_SUCCESS, or WS_EXIT_INVALID_ARG on failure
 */
int ws_cmd_enable_boot_config(const char *directive, const char *match,
                              const char *what, const char *added_by);

/* ============================================================================
 * JSON Output Builder
 * ============================================================================
 * Helper functions to build JSON output strings incrementally.
 * Handles proper formatting, escaping, and comma placement.
 */

/*
 * JSON builder context structure.
 * Initialize with ws_json_builder_init() before use.
 */
typedef struct {
    char *buffer;           /* Output buffer */
    size_t capacity;        /* Buffer capacity */
    size_t length;          /* Current string length */
    int field_count;        /* Number of fields written (for comma handling) */
    int error;              /* Error flag if buffer overflow */
} ws_json_builder_t;

/*
 * Initialize a JSON builder with a buffer.
 *
 * @param builder   Builder context to initialize
 * @param buffer    Output buffer to write to
 * @param capacity  Size of output buffer
 */
void ws_json_builder_init(ws_json_builder_t *builder, char *buffer, size_t capacity);

/*
 * Start a JSON object (writes '{').
 *
 * @param builder   Builder context
 */
void ws_json_builder_start(ws_json_builder_t *builder);

/*
 * End a JSON object (writes '}').
 *
 * @param builder   Builder context
 */
void ws_json_builder_end(ws_json_builder_t *builder);

/*
 * Add a string field to the JSON object.
 * Value is automatically escaped.
 *
 * @param builder   Builder context
 * @param key       Field name
 * @param value     String value (will be escaped)
 */
void ws_json_builder_add_string(ws_json_builder_t *builder, const char *key, const char *value);

/*
 * Add an integer field to the JSON object.
 *
 * @param builder   Builder context
 * @param key       Field name
 * @param value     Integer value
 */
void ws_json_builder_add_int(ws_json_builder_t *builder, const char *key, long value);

/*
 * Add a double field to the JSON object.
 *
 * @param builder   Builder context
 * @param key       Field name
 * @param value     Double value
 * @param precision Number of decimal places (e.g., 3 for "%.3f")
 */
void ws_json_builder_add_double(ws_json_builder_t *builder, const char *key, double value, int precision);

/*
 * Add a boolean field to the JSON object.
 *
 * @param builder   Builder context
 * @param key       Field name
 * @param value     Boolean value
 */
void ws_json_builder_add_bool(ws_json_builder_t *builder, const char *key, bool value);

/*
 * Add a null field to the JSON object.
 *
 * @param builder   Builder context
 * @param key       Field name
 */
void ws_json_builder_add_null(ws_json_builder_t *builder, const char *key);

/*
 * Get the final JSON string.
 * Returns NULL if there was an error during building.
 *
 * @param builder   Builder context
 * @return          Pointer to the JSON string, or NULL on error
 */
const char *ws_json_builder_get(ws_json_builder_t *builder);

/* ============================================================================
 * JSON Array Builder
 * ============================================================================
 * Accumulates JSON values into an array, growing as needed, so a driver does
 * not size a buffer up front for a reading count it cannot know. Each driver
 * previously open-coded this with realloc and strcat, re-measuring the whole
 * string per reading.
 */

/*
 * JSON array builder context.
 * Initialise with ws_json_array_init() and release with ws_json_array_free().
 * Treat the fields as private.
 */
typedef struct {
    char *buffer;           /* Heap buffer, owned by the builder */
    size_t capacity;        /* Allocated size */
    size_t length;          /* Current string length, excluding terminator */
    int item_count;         /* Items added, for comma placement */
    int error;              /* Set if an allocation failed */
} ws_json_array_builder_t;

/*
 * Initialise a JSON array builder, allocating its buffer.
 *
 * @param builder   Builder context to initialise
 * @return          0 on success, -1 if the allocation failed
 */
int ws_json_array_init(ws_json_array_builder_t *builder);

/*
 * Add an item to the array, growing the buffer as needed.
 *
 * Commas are placed for you. A failed allocation is remembered rather than
 * reported here, so a caller may add freely and check once at the end: the
 * builder then yields NULL from ws_json_array_get().
 *
 * An empty item is treated the same way. No JSON value is empty, so one can
 * only be a reading whose builder failed and cleared its buffer; appending it
 * would produce "[,]", which is not JSON at all. The array fails as a whole
 * rather than pass that off as output.
 *
 * @param builder   Builder context
 * @param item      A complete JSON value
 */
void ws_json_array_add(ws_json_array_builder_t *builder, const char *item);

/*
 * Finalise the array, appending the closing bracket.
 *
 * @param builder   Builder context
 */
void ws_json_array_end(ws_json_array_builder_t *builder);

/*
 * Get the finished array.
 *
 * @param builder   Builder context
 * @return          The JSON array, or NULL if any allocation failed. Valid
 *                  until ws_json_array_free().
 */
const char *ws_json_array_get(ws_json_array_builder_t *builder);

/*
 * Release the builder's buffer. Safe on a failed init, and safe twice.
 *
 * @param builder   Builder context
 */
void ws_json_array_free(ws_json_array_builder_t *builder);

/* ============================================================================
 * Sensor JSON Helpers
 * ============================================================================
 * High-level helpers for building sensor output JSON.
 */

/*
 * Format a Unix timestamp as a string.
 *
 * @param buffer    Output buffer
 * @param bufsize   Size of output buffer
 * @param timestamp Unix timestamp
 */
void ws_format_timestamp(char *buffer, size_t bufsize, time_t timestamp);

/*
 * Build base sensor JSON from sc-prototype template.
 * Populates common fields: sensor, device, measures, unit, sensor_id,
 * sensor_name, location, internal, timestamp.
 * Returns the modified JSON in output buffer for sensor-specific additions.
 *
 * Every string is escaped here. Pass them as they are, unescaped: a
 * sensor_id or sensor_name comes from a config file someone typed, and a quote
 * in it must not be able to end the string early and invalidate the document.
 * Escaping at the call site instead had been done by one driver, for one
 * field, and would now double-escape.
 *
 * @param output        Output buffer for JSON
 * @param output_len    Size of output buffer
 * @param sensor        Sensor type (e.g., "dht11_temperature", "ds18b20")
 * @param device        Physical device model (e.g., "dht11", "bme680",
 *                      "ds18b20"), or NULL if the driver has none to name.
 *                      Distinct from `sensor`, which may combine device and
 *                      measurand; this is what identifies the STA Sensor.
 * @param measures      What is measured (e.g., "temperature", "humidity")
 * @param unit          Unit of measurement (e.g., "Celsius", "percentage")
 * @param sensor_id     Unique sensor identifier, or NULL to leave null
 * @param sensor_name   Human-readable name (can be NULL)
 * @param internal      true if internal sensor
 * @param location      Where the sensor is, from its config; NULL or
 *                      WS_LOC_UNDECLARED leaves the field null
 * @param timestamp     Unix timestamp of reading
 * @return              0 on success, -1 on error (prototype not available, or
 *                      out of memory); output is then an empty string
 */
int ws_build_sensor_json_base(char *output, size_t output_len,
                               const char *sensor, const char *device,
                               const char *measures, const char *unit,
                               const char *sensor_id, const char *sensor_name,
                               bool internal, const ws_location_t *location,
                               time_t timestamp);

/*
 * Add value field to sensor JSON.
 * Use after ws_build_sensor_json_base().
 *
 * @param json      JSON buffer to modify
 * @param value     Numeric value
 * @param precision Decimal places (e.g., 1 for "23.5")
 */
void ws_sensor_json_set_value(char *json, double value, int precision);

/*
 * Add error field to sensor JSON and set value to null.
 * Use after ws_build_sensor_json_base().
 *
 * Where the reading may or may not have failed, prefer
 * ws_sensor_json_set_result(), which cannot emit a value alongside the error.
 *
 * The message is escaped in full, however long. A message that then does not
 * fit the buffer is refused whole rather than clipped, as for any string.
 *
 * @param json          JSON buffer to modify
 * @param json_capacity Size of the buffer
 * @param error_msg     Error message (will be escaped)
 */
void ws_sensor_json_set_error(char *json, size_t json_capacity,
                              const char *error_msg);

/*
 * Set a reading's outcome: a value or an error, never both.
 *
 * Exactly one field is populated. A NULL or empty error_msg means the reading
 * succeeded, so "value" is set and "error" stays null; otherwise "error" is
 * set and "value" stays null.
 *
 * Prefer this to choosing between ws_sensor_json_set_value() and
 * ws_sensor_json_set_error() at each call site. It keeps the choice in one
 * place, so a driver cannot report a sentinel reading next to the error that
 * says to disregard it.
 *
 * @param json          JSON buffer to modify
 * @param json_capacity Size of the buffer
 * @param value         Numeric value; used only when error_msg means success
 * @param precision     Decimal places for value (e.g. 3 for "21.375")
 * @param error_msg     Error message (escaped by the library), or NULL/"" when
 *                      the reading succeeded
 */
void ws_sensor_json_set_result(char *json, size_t json_capacity, double value,
                               int precision, const char *error_msg);

/*
 * Set the config field in sensor JSON to a custom JSON object.
 * Replaces "config":null with "config":<config_json>.
 * Use after ws_build_sensor_json_base().
 *
 * The config_json parameter should be a valid JSON object string,
 * e.g., "{\"software_version\":\"1.0\",\"i2c_addr\":\"0x76\"}"
 *
 * @param json          JSON buffer to modify (must have sufficient capacity)
 * @param json_capacity Size of JSON buffer
 * @param config_json   JSON object string to insert (without surrounding quotes)
 */
void ws_sensor_json_set_config(char *json, size_t json_capacity, const char *config_json);

/*
 * Build a config JSON object with common fields.
 * Returns the built string in the provided buffer.
 * Additional fields can be appended before the closing '}'.
 *
 * @param buffer        Output buffer for config JSON
 * @param bufsize       Size of output buffer
 * @param version       Software version string
 * @return              Number of characters written (excluding null terminator)
 */
int ws_build_config_base(char *buffer, size_t bufsize, const char *version);

/*
 * Append a string field to a config JSON object.
 * Call after ws_build_config_base() and before ws_config_end().
 *
 * @param buffer    Config buffer to append to
 * @param bufsize   Size of buffer
 * @param key       Field name
 * @param value     String value
 * @return          Number of characters appended
 */
int ws_config_add_string(char *buffer, size_t bufsize, const char *key, const char *value);

/*
 * Append an integer field to a config JSON object.
 *
 * @param buffer    Config buffer to append to
 * @param bufsize   Size of buffer
 * @param key       Field name
 * @param value     Integer value
 * @return          Number of characters appended
 */
int ws_config_add_int(char *buffer, size_t bufsize, const char *key, long value);

/*
 * Append a nested JSON object to a config JSON object.
 * Use for calibration data or other nested structures.
 *
 * @param buffer      Config buffer to append to
 * @param bufsize     Size of buffer
 * @param key         Field name
 * @param object_json JSON object string (e.g., "{\"par_t1\":123}")
 * @return            Number of characters appended
 */
int ws_config_add_object(char *buffer, size_t bufsize, const char *key, const char *object_json);

/*
 * Close a config JSON object (replaces trailing ',' with '}' if needed).
 *
 * @param buffer    Config buffer to close
 */
void ws_config_end(char *buffer);

#endif /* WS_UTILS_H */
