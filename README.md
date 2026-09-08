## ESP-IDF Project

```bash
$ idf.py --version
ESP-IDF v6.1
```

## Schematics

Two pullup buttons wired to GND: the AC toggle button on GPIO36 and the DC toggle button on GPIO37.

## Commands

```shell
# Build project (default variant, superloop)
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
