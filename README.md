# CoreDesk

CoreDesk is a C++20 desktop file indexing and LAN transfer client. The
normative implementation specification lives in `docs/COREDESK_SPEC.md`.

## M0 Build

```powershell
cmake -S . -B build -DCOREDESK_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
.\build\Debug\coredesk_cli.exe --version
```

If Qt 6 is not installed yet, the M0 common-library skeleton can be verified
without the UI and network adapter placeholders:

```powershell
cmake -S . -B build-m0-verify -G Ninja -DCOREDESK_BUILD_TESTS=ON -DCOREDESK_BUILD_UI=OFF -DCOREDESK_BUILD_NETWORK=OFF
cmake --build build-m0-verify
ctest --test-dir build-m0-verify --output-on-failure
.\build-m0-verify\coredesk_cli.exe --version
```

M0 intentionally contains only the engineering skeleton, common
`Result`/`Error` types, test wiring, and `coredesk_cli --version`.
