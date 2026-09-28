# Project roadmap

## Reliability and home flashing

- [x] Propagate LED, queue, task, and Matter initialization errors instead of silently continuing.
- [ ] Validate restored NVS values and define explicit factory-reset and recovery behavior.
- [ ] Batch rapid light-state persistence so slider updates do not commit every intermediate value.
- [ ] Pin ESP-IDF, ESP-Matter, and component versions for repeatable builds.
- [ ] Document tested board and LED combinations as they are verified.
- [ ] Add repeatable host-side coverage for color conversion, configuration bounds, and state persistence.

## Optional light features

- [ ] Add per-pixel output, scenes, gradients, and effects after the single-color path is reliable.
- [ ] Improve the commissioning and first-boot experience for people flashing the firmware at home.

## Out of scope

- Matter OTA updates; firmware is built and flashed locally with ESP-IDF.
