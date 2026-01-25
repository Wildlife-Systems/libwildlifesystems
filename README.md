# ws-utils

WildlifeSystems shared sensor utilities - a static library providing common functionality for sensor drivers.

## Features

- **JSON utilities**: String escaping, null value replacement for strings, numbers, booleans
- **Prototype caching**: Integration with `sc-prototype` for JSON templates
- **Command handlers**: Standard `identify`, `list`, and `version` command implementations
- **System utilities**: GPIO pin validation, Raspberry Pi serial number retrieval
- **Standard exit codes**: `WS_EXIT_SUCCESS`, `WS_EXIT_IDENTIFY`, `WS_EXIT_INVALID_ARG`

## Installation

### From Debian package

```bash
sudo dpkg -i libws-utils-dev_1.0.0_armhf.deb
```

### From source

```bash
make
sudo make install
```

## Usage

Include the header and link against the static library:

```c
#include <ws/ws_utils.h>
```

Compile with:
```bash
gcc -o my_sensor my_sensor.c -lws_utils
```

Or with the include path:
```bash
gcc -I/usr/include/ws -o my_sensor my_sensor.c -L/usr/lib -lws_utils
```

## API Reference

### Exit Codes

```c
#define WS_EXIT_SUCCESS     0
#define WS_EXIT_IDENTIFY    60
#define WS_EXIT_INVALID_ARG 20
```

### Location Filter

```c
typedef enum {
    WS_LOCATION_ALL = 0,
    WS_LOCATION_INTERNAL,
    WS_LOCATION_EXTERNAL
} ws_location_filter_t;
```

### Functions

| Function | Description |
|----------|-------------|
| `ws_json_escape_string()` | Escape string for JSON |
| `ws_json_replace_null_string()` | Replace null with string value |
| `ws_json_replace_null_number()` | Replace null with double value |
| `ws_json_replace_null_int()` | Replace null with long value |
| `ws_json_replace_null_bool()` | Replace null with boolean |
| `ws_get_sc_prototype()` | Get JSON prototype (caller frees) |
| `ws_get_prototype_cached()` | Get cached JSON prototype |
| `ws_get_timestamp()` | Get current Unix timestamp |
| `ws_cmd_identify()` | Handle identify command (exits) |
| `ws_cmd_list_single()` | Handle list with one measurement (exits) |
| `ws_cmd_list_multiple()` | Handle list with multiple measurements (exits) |
| `ws_get_serial_number()` | Get Pi serial number (caller frees) |
| `ws_validate_gpio_pin()` | Validate GPIO pin (2-27) |
| `ws_print_version()` | Print version information |

## License

GPL-2+
