# ESP32 Matter WS2812B Controller

An open-source firmware project for people who want to build and flash a Matter-controlled WS2812B light at home. The goal is reliable DIY use, not a commercial product or certification program.

## What it does today

- Exposes one Matter Extended Color Light endpoint.
- Accepts on/off, brightness, hue/saturation, XY color, and color-temperature updates.
- Renders one solid color across the configured strip; individual pixels and matrix effects are not implemented.
- Saves the last light state in NVS and attempts to restore it at startup.
- Estimates LED current in software and scales output to a configured budget. This is an estimate, not a current sensor or electrical protection circuit.

The firmware and hardware are not validated across every ESP32 board or WS2812B revision. The repository includes default sdkconfig profiles for ESP32-C3 and ESP32-S3; pin choices, RMT support, flash size, and power wiring still need to match your board. Expect to inspect logs and adjust configuration for your hardware.

## Hardware

- An ESP32 board supported by your installed ESP-IDF and ESP-Matter versions.
- A WS2812B-compatible strip or matrix connected to the configured data GPIO.
- A suitably rated external 5 V supply for the LEDs. Do not draw the strip's load current through the ESP32 development board.
- A common ground between the ESP32 and LED supply. For a 5 V strip, use a logic-level shifter when the board's 3.3 V data signal is not accepted reliably.
- Appropriate wiring, connectors, and overcurrent protection for the selected LED count and supply.

The default GPIO and current budget are examples, not recommendations for every board or strip. Check the board pinout before connecting the data line. The software current estimate cannot protect against an undersized supply, wiring fault, or failed firmware.

## Build and flash

Use the pinned source revisions below for repeatable builds. The CMake configure step checks these revisions and stops with a clear error when an SDK checkout differs. After checking out ESP-Matter, initialize its submodules at the revisions recorded by that checkout.

| Build input | Pinned revision |
| --- | --- |
| ESP-IDF | v5.5.3, `2c211b236707889e8400c4dc5644dd5c4ee071e0` |
| ESP-Matter | `94d54bc3353c1740a358dad1328ea4c20feebe5d` |
| ConnectedHomeIP submodule | `8f943388af4d12dc5c484eae21b22723e03c3616` |
| ESP Component Manager registry dependencies | Exact versions in the checked-in `dependencies.lock` |

Direct dependencies are pinned in `main/idf_component.yml`. The Component Manager generates `dependencies.lock`; do not edit it by hand. The checked-in lock currently targets `esp32c3`; switching targets can trigger a target-specific lock regeneration. When intentionally changing a dependency or SDK pin, reconfigure and build with the matching target, review the generated lockfile, update the revision checks in `cmake/check-build-versions.cmake`, and verify the supported target profiles before committing the change.

Install the ESP-IDF tools from the pinned ESP-IDF checkout and export both SDK environments so `IDF_PATH`, `ESP_MATTER_PATH`, and the required ESP-Matter device path are available to CMake.

Select your target and configure the project:

```bash
idf.py set-target esp32c3
idf.py menuconfig
```

In **WS2812B Configuration**, set the data GPIO, LED count, current budget, and brightness cap for your hardware. Confirm the target-specific pin and flash configuration before building.

```bash
idf.py build
idf.py flash monitor
```

If Ninja reports that `build.ninja` cannot load `CMakeFiles/rules.ninja`, regenerate the build files with `idf.py reconfigure`, then run `idf.py build`. When changing chips, run `idf.py set-target <target>` before building so ESP-IDF and ESP-Matter use the same target.

This project does not use Matter OTA updates. Firmware updates are performed locally over the ESP32's supported flashing interface. Keep a copy of the matching ESP-IDF and ESP-Matter versions used to build your firmware.

## Matter commissioning and recovery

On first boot, follow the commissioning information printed by the firmware monitor and add the light from your Matter controller. The current defaults are development/test settings; they are suitable for local experimentation, not for reusing as shared credentials across a group of devices.

The firmware does not assume a particular board button. To factory reset a running device, connect to its serial console after Matter starts and run:

```text
matter esp factoryreset
```

This erases the saved light settings and Matter pairing data, then restarts the device. Commission it again after the restart. If the firmware cannot boot far enough to accept the command, erase and reflash it locally:

```bash
idf.py -p PORT erase-flash
idf.py -p PORT flash monitor
```

Replace `PORT` with the serial port for the board. A full flash erase clears all NVS data, including Matter pairing and saved light settings. With the repository's default configuration, ESP-Matter and the light state share the `nvs` partition; if NVS reports no free pages or an incompatible version at startup, the firmware automatically erases that partition and logs that recommissioning may be required. Individual saved values with an unsupported type or range fall back to safe defaults and are reported in the serial log. Light-state writes are coalesced in 500 ms batches; sudden power loss can lose the most recent changes made within that window.

Brightness, color, and on/off changes are eased at the LED output over a few hundred milliseconds. Matter attributes continue to reflect the requested target immediately.

## Limitations

- One endpoint and a uniform strip color are implemented; scenes, gradients, per-pixel addressing, and effects remain future work.
- Color output depends on the LED revision and hardware; software values are not a calibrated colorimeter.
- No over-the-air update path is planned.
- Builds and source review do not prove radio range, electrical safety, power-limit accuracy, or reliability on a particular assembled device.

## License

MIT; see [LICENSE](LICENSE).
