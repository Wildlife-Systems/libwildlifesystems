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
#include "ws_utils.h"

/* Cached prototype from sc-prototype */
static char *g_prototype_cache = NULL;
static bool g_prototype_loaded = false;

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
