# Contributing

Keep conversion fail-closed and add validation for every newly accepted NIF
shape or version. Build from a recursive checkout before opening a pull request:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Do not commit NIFs, textures, plugins, or other mod assets unless their license
and redistribution permission are explicitly documented.
