# libobi
## Omni Backstage Interface (OBI) Loader/Runtime

**Last Updated:** 2026-03-01  
**Status:** Draft  
**Repository Role:** Implementation (loader/runtime), not the OBI spec

---

`libobi` is a small C library intended to make OBI-based hosts easy to ship:

- dynamically (or statically) load OBI providers,
- select providers at runtime,
- expose a simple API for fetching profile handles from the selected providers.

Design goals:

- dependency-free core runtime (just libc + platform dynamic loading),
- canonical ABI headers sourced from `OBI-ABI` (tracked as the `obi-abi/` submodule),
- provider plugins wrap third-party libraries and remain separately distributable,
- stable C ABI surface suitable for FFIs.

---

## Build

This repo uses Meson.

```sh
meson setup build
meson compile -C build
```

---

## Notes on Licensing

The `libobi` runtime itself is MPL-2.0. Provider plugins are separate modules and may carry their
own licenses depending on the libraries they wrap. Distributions can ship a curated provider pack
without forcing all hosts to take on all third-party dependencies.
