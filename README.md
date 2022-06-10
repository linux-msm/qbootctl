# Qualcomm bootctl HAL for Linux

This HAL was pulled from AOSP source code and bastardised to build and run on a musl/glibc system. This may or may not render any hardware you run it on unusable, you have been warned.

## Dependencies

* zlib-dev
* meson
* cmake
* linux-headers

## Building

qbootctl uses the meson build system

```sh
meson build
meson compile -C build
```

## Usage

```text
qbootctl: qcom bootctrl HAL port for Linux
-------------------------------------------
qbootctl [-c|-m|-s|-u|-b|-n|-x] [SLOT]

    <no args>        dump slot info (default)
    -h               this help text
    -c               get the current slot
    -b SLOT          check if SLOT is marked as bootable
    -n SLOT          check if SLOT is marked as successful
    -x [SLOT]        get the slot suffix for SLOT (default: current)
    -s SLOT          set to active slot to SLOT
    -m [SLOT]        mark a boot as successful (default: current)
    -u [SLOT]        mark SLOT as unbootable (default: current)
```

## Debugging

Set `DEBUG` to 1 in `utils.h` to enable debug logging.

## Documentation

A more details explanation and a list of devices where qbootctl has been
validated can be found [on the postmarketOS wiki](https://wiki.postmarketos.org/wiki/Android_AB_Slots):
