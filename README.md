# libwildlifesystems

libwildlifesystems is a static C library providing the functionality shared by
WildlifeSystems sensor drivers. A driver built on it contains only what is
specific to its hardware; the reading format, configuration handling, location
handling and the standard commands are provided here and are therefore the same
in every driver.

Two Debian packages are built from this source.

- `libwildlifesystems-dev` provides the static library `libwildlifesystems.a`
  and the header `ws/ws_utils.h`, used to build sensor drivers written in C.
- `wildlifesystems-tools` provides `ws-emit`, which produces reading JSON from
  readings described on the command line. It allows a driver written in shell,
  such as `sensor-onboard`, to produce exactly the output of the C drivers. See
  `ws-emit(1)`.

## Functionality

- **Readings.** Reading JSON is built from the `sc-prototype` template with the
  common fields filled in. Every string is escaped by the library. A reading
  carries either a value or an error, never both.
- **Configuration.** An iterator over `/etc/ws/sensors/<driver>.json` parses
  the fields common to every driver (`internal`, `sensor_id`, `sensor_name`
  and `location`) and returns each entry so that the driver can read its own
  fields. Entries without a `sensor_id` are given one derived from the node
  serial number, without collisions.
- **Location.** The position of the node is read from `/etc/geolocation` and
  the position of a sensor from its configuration. Both are rendered as
  GeoJSON, and the tokens `{{node}}` and `{{none}}` are resolved.
- **Commands.** The `identify`, `list`, `mock` and `enable` commands, the usage
  line and the handling of unknown arguments are implemented once. Each reads
  the driver's list of measurements, so that what a driver lists, accepts and
  prints in its usage line cannot differ.
- **Units.** The canonical spellings of units. A misspelt unit does not fail; it
  creates a second datastream for the same quantity downstream.
- **Logging.** Messages are written to stderr and syslog together.
- **Raspberry Pi.** The node serial number, validation of GPIO pin numbers, and
  editing of the boot configuration to enable a bus.

## Building

The library and `ws-emit` are built with `make`, and the unit tests are run
with `make test`.

```bash
make
make test
sudo make install
```

`make uninstall` removes what `make install` placed. The unit tests are also
run when the Debian package is built, so a build fails if any test fails. The
tests do not depend on the host: a Raspberry Pi has a serial number and may
have a surveyed location, and the tests point the library away from both.

## Using the library

A driver includes the header and links against the static library.

```c
#include <ws_utils.h>
```

```bash
gcc -I/usr/include/ws -o sensor-example sensor-example.c -lwildlifesystems
```

The `main` function of a driver is short. It parses its single argument, calls
`ws_require_prototype()`, loads its configuration with `ws_config_iter_open()`,
`ws_config_iter_next()` and `ws_config_iter_close()`, reads the hardware, and
for each measurement calls `ws_build_sensor_json_base()` followed by
`ws_sensor_json_set_result()`, collecting the readings with the
`ws_json_array_*` functions. The three C drivers, `sensor-dht11`,
`sensor-bme680` and `sensor-w1therm`, are the reference implementations.

## API

Each function is documented in `src/ws_utils.h`. The tables below list them by
area.

### Constants

| Name | Description |
|------|-------------|
| `WS_EXIT_SUCCESS`, `WS_EXIT_INVALID_ARG` (20), `WS_EXIT_IDENTIFY` (60) | Exit codes used by every driver |
| `WS_UNIT_CELSIUS`, `WS_UNIT_PERCENTAGE`, `WS_UNIT_HPA`, `WS_UNIT_OHMS` | Canonical spellings of units |
| `WS_CONFIG_PATH(name)` | The path `/etc/ws/sensors/<name>.json` |
| `ws_location_filter_t` | `WS_LOCATION_ALL`, `WS_LOCATION_INTERNAL`, `WS_LOCATION_EXTERNAL` |

### Commands and arguments

| Function | Description |
|----------|-------------|
| `ws_cmd_identify()` | Exit with code 60 |
| `ws_cmd_list_multiple()` | Print the measurements and exit |
| `ws_cmd_usage()`, `ws_cmd_unknown_arg()` | Print the usage line; report an unrecognised argument and return 20 |
| `ws_arg_is_measurement()` | Test whether an argument names one of the driver's measurements |
| `ws_location_filter_matches()` | Test whether a sensor passes the `internal` or `external` filter |
| `ws_cmd_mock()` | Emit a table of fixed readings in the real output format |
| `ws_cmd_enable_boot_config()` | Implement `enable` for a bus that requires a boot configuration directive |
| `ws_print_version()` | Print the version line |
| `ws_require_prototype()` | Confirm that the `sc-prototype` template is available before any reading is taken |

### Readings

| Function | Description |
|----------|-------------|
| `ws_build_sensor_json_base()` | Fill the common fields of the template, escaping every string |
| `ws_sensor_json_set_result()` | Set the value or the error of a reading, never both |
| `ws_sensor_json_set_value()`, `ws_sensor_json_set_error()` | Set the value, or the error, alone |
| `ws_sensor_json_set_config()` | Set the driver-specific `config` object |
| `ws_measurement_id()` | Build `<sensor_id>_<measurement>`; a NULL id remains NULL |
| `ws_json_array_init()`, `ws_json_array_add()`, `ws_json_array_end()`, `ws_json_array_get()`, `ws_json_array_free()` | Build the output array, which grows as required |
| `ws_json_builder_init()`, `ws_json_builder_start()`, `ws_json_builder_add_string()`, `ws_json_builder_add_int()`, `ws_json_builder_add_double()`, `ws_json_builder_add_bool()`, `ws_json_builder_add_null()`, `ws_json_builder_add_raw()`, `ws_json_builder_end()`, `ws_json_builder_get()` | Build a JSON object, such as the `config` object |
| `ws_unit_canonical()` | Map a unit name to its canonical spelling |

### Configuration

| Function | Description |
|----------|-------------|
| `ws_config_iter_open()`, `ws_config_iter_next()`, `ws_config_iter_close()` | Iterate over the entries of a driver's configuration file; the common fields are parsed into a `ws_sensor_config_base_t` |
| `ws_config_assign_fallback_ids()` | Give entries without a `sensor_id` one derived from the node serial number |
| `ws_sensor_config_free_fields()` | Free the strings of a base configuration |
| `ws_json_parse_string()`, `ws_json_parse_int()`, `ws_json_parse_double()`, `ws_json_parse_bool()`, `ws_json_parse_object()` | Read a driver's own field from an entry |
| `ws_json_count_objects()`, `ws_json_object_end()` | Scan JSON at a lower level |

### Location

| Function | Description |
|----------|-------------|
| `ws_read_geolocation()`, `ws_geolocation_geojson()` | Read the position of the node from `/etc/geolocation` and render it |
| `ws_parse_sensor_location()` | Parse the `location` field of a configuration entry |
| `ws_location_from_token()` | Parse `{{node}}` or `{{none}}` |
| `ws_location_json()` | Render a location for a reading |

### JSON

| Function | Description |
|----------|-------------|
| `ws_json_escape_string()` | Escape quotes and backslashes |
| `ws_json_replace_null_string()`, `ws_json_replace_null_number()`, `ws_json_replace_null_int()`, `ws_json_replace_null_bool()`, `ws_json_replace_null_raw()` | Replace `"key":null` in place; a value that does not fit is refused rather than clipped |
| `ws_get_sc_prototype()`, `ws_get_prototype_cached()` | Obtain the template from `sc-prototype` |

### System

| Function | Description |
|----------|-------------|
| `ws_log_init()`, `ws_log_error()`, `ws_log_warning()`, `ws_log_info()` | Log to stderr and syslog |
| `ws_get_serial_number()`, `ws_get_serial_with_suffix()` | Read the Raspberry Pi serial number |
| `ws_validate_gpio_pin()` | Test that a GPIO pin number is between 2 and 27 |
| `ws_read_file()` | Read a file into a buffer |
| `ws_boot_config_path()`, `ws_boot_config_has()`, `ws_boot_config_add()` | Locate, inspect and append to the Raspberry Pi boot configuration |

## License

GPL-2+
