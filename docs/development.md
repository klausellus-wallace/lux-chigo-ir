# Development guide

## Repo layout

```
lux-chigo-ir/
├── components/lux_chigo_ir/  # ESPHome component (Python + C++)
├── tests/                    # host-native C++ tests
│   ├── captures/             # raw ESPHome log fixtures
│   ├── build_fixtures.py     # extract Frame-B fixtures → fixtures.json
│   ├── fixtures.json         # generated ground-truth set
│   ├── test_protocol.cpp     # differential + edge-case tests
│   └── Makefile
├── examples/                 # ready-to-copy YAML for downstream users
├── docs/                     # protocol + methodology + hardware
└── README.md
```

## Running the C++ tests

```bash
cd tests
python3 build_fixtures.py         # regenerate fixtures.json from captures/
make run                          # compile and run test_protocol
```

Expected: `rt_ok=N/N ao_ok=M/M failures=0` — every roundtrip fixture
must reproduce byte-exactly, every accept-only fixture must not crash
the decoder.

## Regenerating fixtures

If you record new IR captures with a similar remote, drop them into
`tests/captures/*.log` and re-run `python3 build_fixtures.py`.

## Building the firmware

The component is used via ESPHome's `external_components:` mechanism.
See `examples/lux-ac-ir.example.yaml`. Docker build:

```bash
docker exec <your-esphome-container> esphome compile /config/<your>.yaml
docker exec <your-esphome-container> esphome upload /config/<your>.yaml --device <ip>
```

## Adding a new AC feature

1. Add the byte-level encoding decision to `docs/protocol.md`.
2. Add fields to `LuxState` in `components/lux_chigo_ir/protocol.h`.
3. Implement encode/decode in `protocol.cpp`.
4. Add unit tests in `tests/test_protocol.cpp` — at least one pinned
   fixture and one round-trip case.
5. Expose to Home Assistant in `components/lux_chigo_ir/lux_chigo_ir.cpp`
   (climate fields, presets, or a template switch — see the HEALTH
   pattern for reference).
6. Bump `CHANGELOG.md`.
