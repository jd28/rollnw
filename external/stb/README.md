# Vendored stb / SOIL2

## Provenance

| File | Version | Upstream |
| --- | --- | --- |
| `stb_image.h` | v2.30 (2024-05-31) | [nothings/stb](https://github.com/nothings/stb) @ `2c980bb59875b0d32144a71867fbdebb2f77cd20` |
| `stb_image_write.h` | v1.05 | [nothings/stb](https://github.com/nothings/stb) — **outdated**, upstream is v1.16 |
| `stb_image_resize.h` | v0.96 | [nothings/stb](https://github.com/nothings/stb) — **deprecated upstream**, replaced by `stb_image_resize2.h` (different API) |
| `stb_dxt.h`, `stb_truetype.h` | as imported | [nothings/stb](https://github.com/nothings/stb) |
| `stbi_DDS*.h`, `stbi_pvr*.h`, `stbi_pkm*.h`, `stbi_ext*.h`, `image_DXT.*`, `wfETC.*`, `pvr_helper.h`, `pkm_helper.h` | as imported | [SpartanJ/SOIL2](https://github.com/SpartanJ/SOIL2) |

## Local modifications to `stb_image.h`

`stb_image.h` is **not** stock stb. It carries the SOIL2 patch, which hooks the DDS, PVR and PKM
loaders into stb's format dispatch. This is load-bearing: `nw::Image::parse_dxt()`
(`lib/nw/formats/Image.cpp`) decodes DDS by calling plain `stbi_load_from_memory()` and relies on
`stbi__dds_test`/`stbi__dds_load` being reached from `stbi__load_main`.

The patch is five hunks, all additive, and must be re-applied when updating `stb_image.h`:

1. Public header: `#include` of `stbi_DDS.h` / `stbi_pvr.h` / `stbi_pkm.h` / `stbi_ext.h`, each
   guarded by `STBI_NO_DDS` / `STBI_NO_PVR` / `STBI_NO_PKM` / `STBI_NO_EXT`.
2. Forward declarations of `stbi__dds_*`, `stbi__pvr_*`, `stbi__pkm_*`.
3. Dispatch in `stbi__load_main`, placed after the PNM test and before the HDR test.
4. Dispatch in `stbi__info_main`, placed after the HDR test and before the TGA test.
5. Implementation `#include`s of `stbi_DDS_c.h` / `stbi_pvr_c.h` / `stbi_pkm_c.h` / `stbi_ext_c.h`
   at the end of the `STB_IMAGE_IMPLEMENTATION` section.

To regenerate the patch after an update:

```sh
diff -u <stock stb_image.h> external/stb/stb_image.h
```

Anything beyond those five hunks is unintended drift.

### Dropped during the v2.25 → v2.30 update

Two divergences present in the old v2.25 copy were deliberately **not** carried forward:

- SOIL2 deleted stb's `#if defined(__GNUC__) && defined(STBI__X86_TARGET) && !defined(__SSE2__)`
  → `#define STBI_NO_SIMD` guard. Upstream's guard is restored. It is inert on x86-64 (the
  condition needs 32-bit `STBI__X86_TARGET`), and without it a 32-bit GCC build without `-msse2`
  fails to compile stb's SSE2 intrinsics.
- A whitespace-only change to the `stbi__jpeg_load` signature.
