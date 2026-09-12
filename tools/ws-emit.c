/*
 * ws-emit - emit sensor readings as WildlifeSystems JSON
 * Copyright (C) 2026 Wildlife Systems
 *
 * Part of the WildlifeSystems project.
 * https://wildlife.systems
 *
 * Lets a driver written in shell produce exactly the same reading JSON as the
 * C drivers, rather than reimplementing the sc-prototype template, the field
 * substitution, the unit spellings and the location tokens in jq. Everything
 * emitted comes from libwildlifesystems, so a change there reaches the shell
 * drivers at the same time as the C ones.
 *
 * Usage:
 *   ws-emit [common options] -- <reading> [-- <reading> ...]
 *
 * Common options apply to every reading:
 *   --device NAME     physical device model, e.g. raspberry_pi
 *   --prefix ID       sensor_id prefix; each id becomes PREFIX_<sensor>.
 *                     Without it (and without --id) sensor_id is null: an
 *                     unknown id is recorded as unknown, not fabricated
 *   --name NAME       sensor_name for every reading
 *   --internal        mark every reading as internal
 *
 * Each reading follows a -- separator:
 *   --sensor NAME     e.g. onboard_cpu                  (required)
 *   --measures NAME   e.g. temperature                  (required)
 *   --unit NAME       celsius, percentage, hPa, Ohms    (required)
 *   --value NUM       the reading        (required unless --error is given)
 *   --precision N     decimal places, default 3
 *   --location TOKEN  {{node}} or {{none}}; omit for an undeclared location
 *   --id ID           explicit sensor_id, overriding --prefix
 *   --device NAME     this reading's device, overriding the common one;
 *                     an empty NAME means it has none
 *   --error MSG       report a failed reading; value is left null
 *
 * Prints one JSON array of all the readings. Exits WS_EXIT_SUCCESS, or
 * WS_EXIT_INVALID_ARG on a usage, unit or template error.
 *
 * License: GPL-2+
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ws_utils.h>

#ifndef VERSION
#define VERSION "unknown"
#endif

#define MAX_READINGS 32
#define READING_JSON_SIZE 2048

/* One reading, as described on the command line. */
typedef struct {
    const char *sensor;
    const char *measures;
    const char *unit;       /* canonical spelling, from ws_unit_canonical() */
    ws_location_t location; /* parsed during argument handling */
    const char *id;         /* explicit sensor_id, or NULL to use the prefix */
    const char *device;     /* per-reading device, valid when has_device */
    const char *error;      /* NULL when the reading succeeded */
    double value;
    int precision;
    bool has_value;
    bool has_device;        /* --device given for this reading, even if "" */
} reading_t;

static void usage(void) {
    fprintf(stderr,
        "Usage: ws-emit [--device NAME] [--prefix ID] [--name NAME] [--internal]\n"
        "               -- --sensor NAME --measures NAME --unit NAME\n"
        "                  (--value NUM [--precision N] | --error MSG)\n"
        "                  [--location TOKEN] [--id ID] [--device NAME]\n"
        "               [-- ...]\n");
}

/*
 * The reading's sensor_id: --id if given, else <prefix>_<sensor>, else none.
 *
 * None is deliberate. With neither a prefix nor an explicit id there is
 * nothing to identify the sensor by, and the C drivers' rule applies: an
 * unknown id is recorded as null, not fabricated. The bare sensor name used
 * to be emitted instead, which is the same string on every node and so
 * collides across them.
 *
 * @param out  Receives an allocated id, or NULL for none. Caller frees.
 * @return     0, or -1 if out of memory
 */
static int resolve_id(const reading_t *r, const char *prefix, char **out) {
    size_t len;

    *out = NULL;
    if (r->id) {
        *out = strdup(r->id);
        return *out ? 0 : -1;
    }
    if (!prefix || !*prefix) return 0;

    len = strlen(prefix) + strlen(r->sensor) + 2;
    *out = malloc(len);
    if (!*out) return -1;

    snprintf(*out, len, "%s_%s", prefix, r->sensor);
    return 0;
}

int main(int argc, char *argv[]) {
    const char *device = NULL;
    const char *prefix = NULL;
    const char *name = NULL;
    bool internal = false;
    reading_t readings[MAX_READINGS];
    int count = 0;
    int i;
    time_t now;

    ws_log_init("ws-emit");

    if (argc >= 2 && (strcmp(argv[1], "--version") == 0 ||
                      strcmp(argv[1], "-v") == 0 ||
                      strcmp(argv[1], "version") == 0)) {
        ws_print_version("ws-emit", VERSION);
        return WS_EXIT_SUCCESS;
    }
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        usage();
        return WS_EXIT_SUCCESS;
    }

    memset(readings, 0, sizeof(readings));

    /* Common options, up to the first -- separator. */
    for (i = 1; i < argc && strcmp(argv[i], "--") != 0; i++) {
        if (strcmp(argv[i], "--internal") == 0) {
            internal = true;
        } else if (i + 1 >= argc) {
            ws_log_error("Option %s needs a value", argv[i]);
            return WS_EXIT_INVALID_ARG;
        } else if (strcmp(argv[i], "--device") == 0) {
            device = argv[++i];
        } else if (strcmp(argv[i], "--prefix") == 0) {
            prefix = argv[++i];
        } else if (strcmp(argv[i], "--name") == 0) {
            name = argv[++i];
        } else {
            ws_log_error("Unknown option %s", argv[i]);
            usage();
            return WS_EXIT_INVALID_ARG;
        }
    }

    /* One reading per -- separated group. */
    while (i < argc) {
        reading_t *r;

        if (count >= MAX_READINGS) {
            ws_log_error("Too many readings (maximum %d)", MAX_READINGS);
            return WS_EXIT_INVALID_ARG;
        }
        r = &readings[count];
        r->precision = 3;

        i++;  /* step over the separator */
        for (; i < argc && strcmp(argv[i], "--") != 0; i++) {
            if (i + 1 >= argc || strcmp(argv[i + 1], "--") == 0) {
                ws_log_error("Option %s needs a value", argv[i]);
                return WS_EXIT_INVALID_ARG;
            }
            if (strcmp(argv[i], "--sensor") == 0) {
                r->sensor = argv[++i];
            } else if (strcmp(argv[i], "--measures") == 0) {
                r->measures = argv[++i];
            } else if (strcmp(argv[i], "--unit") == 0) {
                /* Rejecting an unknown unit is the point of routing shell
                   drivers through here: a typo must not reach the output and
                   silently split a Datastream. */
                i++;
                r->unit = ws_unit_canonical(argv[i]);
                if (!r->unit) {
                    ws_log_error("Unknown unit %s", argv[i]);
                    return WS_EXIT_INVALID_ARG;
                }
            } else if (strcmp(argv[i], "--value") == 0) {
                char *endptr;
                i++;
                r->value = strtod(argv[i], &endptr);
                if (endptr == argv[i] || *endptr != '\0') {
                    ws_log_error("Value %s is not a number", argv[i]);
                    return WS_EXIT_INVALID_ARG;
                }
                r->has_value = true;
            } else if (strcmp(argv[i], "--precision") == 0) {
                i++;
                r->precision = atoi(argv[i]);
                if (r->precision < 0 || r->precision > 10) {
                    ws_log_error("Precision %s is out of range (0-10)", argv[i]);
                    return WS_EXIT_INVALID_ARG;
                }
            } else if (strcmp(argv[i], "--location") == 0) {
                /* Tokens only; the library says which, and logs a bad one. */
                if (ws_location_from_token(argv[++i], &r->location) != 0) {
                    return WS_EXIT_INVALID_ARG;
                }
            } else if (strcmp(argv[i], "--id") == 0) {
                r->id = argv[++i];
            } else if (strcmp(argv[i], "--device") == 0) {
                /* Overrides the common --device for this reading alone. An
                   empty name means the reading has no device: a pseudo-sensor
                   such as storage_used measures nothing physical, and must
                   not be tagged with the Pi it happens to run on. */
                r->device = argv[++i];
                r->has_device = true;
            } else if (strcmp(argv[i], "--error") == 0) {
                r->error = argv[++i];
            } else {
                ws_log_error("Unknown reading option %s", argv[i]);
                usage();
                return WS_EXIT_INVALID_ARG;
            }
        }

        if (!r->sensor || !r->measures || !r->unit) {
            ws_log_error("Each reading needs --sensor, --measures and --unit");
            return WS_EXIT_INVALID_ARG;
        }
        if (!r->has_value && !r->error) {
            ws_log_error("Reading %s needs --value or --error", r->sensor);
            return WS_EXIT_INVALID_ARG;
        }
        count++;
    }

    if (count == 0) {
        ws_log_error("No readings given");
        usage();
        return WS_EXIT_INVALID_ARG;
    }

    /* Fail before emitting anything, rather than leaving a half-written
       array behind: every reading needs the template, so check it once here. */
    if (ws_require_prototype() != 0) {
        return WS_EXIT_INVALID_ARG;
    }

    /* One timestamp for the batch: these readings are taken together. */
    now = time(NULL);

    printf("[");
    for (i = 0; i < count; i++) {
        reading_t *r = &readings[i];
        char json[READING_JSON_SIZE];
        char *sensor_id;

        if (resolve_id(r, prefix, &sensor_id) != 0) {
            ws_log_error("Out of memory building sensor_id");
            return WS_EXIT_INVALID_ARG;
        }

        if (ws_build_sensor_json_base(json, sizeof(json), r->sensor,
                                      r->has_device ? r->device : device,
                                      r->measures, r->unit, sensor_id, name,
                                      internal, &r->location, now) != 0) {
            ws_log_error("sc-prototype failed - cannot generate JSON");
            free(sensor_id);
            return WS_EXIT_INVALID_ARG;
        }
        free(sensor_id);

        ws_sensor_json_set_result(json, sizeof(json), r->value, r->precision,
                                  r->error);

        if (i > 0) printf(",");
        printf("%s", json);
    }
    printf("]\n");

    return WS_EXIT_SUCCESS;
}
