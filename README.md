## ESP-IDF Project

```bash
$ idf.py --version
ESP-IDF v6.1
```

## Schematics

Two pullup buttons wired to GND: the AC toggle button on GPIO36 and the DC toggle button on GPIO37.

## NVS

`nvs.csv` holds per-device values and is gitignored. Copy the example and fill in your device's values before building:

```shell
$ cp nvs.csv.example nvs.csv
```

| key         | length           | description           |
|-------------|------------------|-----------------------|
| `ef_user`   | 19 digits        | EcoFlow user id       |
| `ef_mac`    | 12 hex chars     | device BLE MAC        |
| `ef_serial` | 16 chars         | device serial number  |

Field sizes are fixed buffers in `main/include/config.h` — a string value must leave room for the NUL terminator (e.g. `ef_serial` is 17 bytes, so at most 16 characters).

## Commands

```shell
# Build project
$ idf.py all

# Flash firmware
$ idf.py flash

# Monitor
$ idf.py monitor

# Clean build files
$ idf.py fullclean
```

## Credits

The RIVER2_PROTOCOL implementation is 100% based on [rabits/ha-ef-ble](https://github.com/rabits/ha-ef-ble)
