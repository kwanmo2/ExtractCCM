# ExtractCCM

Qt6 + OpenCV tool that extracts a color correction matrix (CCM) from a color chart image.

## Build (Windows, VS 2022)

```
cmake --preset conda           # Qt6 / OpenCV from %USERPROFILE%\miniforge3\Library
cmake --build --preset conda   # -> build/conda/Release/ExtractCCM.exe
ctest --preset conda           # core unit tests
```

The post-build step copies the Qt platform plugin (`platforms/qwindows.dll`) and every
dependent DLL next to the exe, so it runs without PATH changes. With the conda OpenCV this
includes Intel MKL (~600 MB). For a small, portable build use the `vcpkg` preset instead
(builds Qt6/OpenCV from source on first run, 30-60 min).

`cmake --install build/conda --prefix dist` produces a folder that can be copied to another PC.

## Usage

1. Capture the chart with the camera's CCM and gamma **off**, evenly lit, no clipping.
2. Open the image (BMP/PNG/TIFF 8/16 bit, or headerless RAW with Bayer demosaic).
3. Set data bits and black level (manual, or estimated from the chart).
4. Pick the illuminant and chart (load the reference file for charts without built-in data).
5. Click the centers of the four corner patches 1 → 2 → 3 → 4, adjust by dragging.
6. Choose the model (3x3, white-preserving 3x3, 3x3+offset, WB+3x3, root-polynomial),
   constraints (coefficient range, non-negative offset, separate exposure gain) and
   optional fixed-point quantization, then **Compute CCM**.
7. Export JSON / CSV / C header / register text, or save the corrected image.

Language: View → Language (English / 한국어), or `--lang en|ko`.

Batch: `ExtractCCM.exe session.ccmproj --export-json result.json` (exit code 0 on success).
Try it with `samples/synthetic_cc24.ccmproj`.

## Notes

- Illuminants are defined by their white point; the reference Lab (D50) values are adapted with
  Bradford CAT. Spectral reference data is not used.
- Built-in reference values exist for ColorChecker Classic 24 (X-Rite post-2014). SG 140,
  SpyderCHECKR and IT8 need their CGATS/CSV reference file.
- Korean UI text: edit `tools/gen_translations.py` and run it to regenerate
  `src/ui/Translations_ko.inc` (it fails if any UI string is untranslated).
