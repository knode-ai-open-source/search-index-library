## 08/22/2025

**Modernize Search Index Library build system, licensing, and tests**

---

## Summary

This PR updates the **Search Index Library** with a modern multi-variant CMake build, unified build tooling, simplified documentation, consistent SPDX licensing headers, and a refreshed test/coverage framework.

---

## Key Changes

### 🔧 Build & Tooling

* **Removed** legacy changelog/config files:

    * `.changes/*`, `.changie.yaml`, `CHANGELOG.md`, `build_install.sh`.
* **Added** `build.sh`:

    * Commands: `build`, `install`, `coverage`, `clean`.
    * Coverage integration with `llvm-cov`.
* `.gitignore`: added new build dirs (`build-unix_makefiles`, `build-cov`, `build-coverage`).
* **BUILDING.md**:

    * Updated for *Search Index Library v0.0.1*.
    * Clear local build + install instructions with `build.sh`.
    * Explicit dependency setup:

        * `a-memory-library`, `the-lz4-library`, `the-macro-library`, `a-tokenizer-library`, `the-io-library`, `zlib`.
    * Modern Dockerfile instructions.
* **Dockerfile**:

    * Ubuntu base image with configurable CMake version.
    * Non-root `dev` user.
    * Builds/installs all required dependencies and this project.

### 📦 CMake

* Raised minimum version to **3.20**.
* Project renamed to `search_index_library` (underscore convention).
* **Multi-variant builds**:

    * `debug`, `memory`, `static`, `shared`.
    * Umbrella alias: `search_index_library::search_index_library`.
* Coverage toggle (`A_ENABLE_COVERAGE`) and memory profiling define (`_AML_DEBUG_`).
* Dependencies declared via `find_package`:

    * `a_memory_library`, `the_macro_library`, `the_io_library`, `a_tokenizer_library`.
* Proper **install/export**:

    * Generates `search_index_libraryConfig.cmake` + version file.
    * Namespace: `search_index_library::`.

### 📖 Documentation

* **AUTHORS**: updated Andy Curtis entry with GitHub profile.
* **NOTICE**:

    * Simplified to list copyright:

        * Andy Curtis (2023–2025), Knode.ai (2024–2025).
    * Dropped inline "technical contact" note (covered in SPDX headers).

### 📝 Source & Headers

* SPDX headers standardized:

    * En-dash year ranges (`2023–2025`, `2024–2025`).
    * Andy Curtis explicitly credited.
    * Knode.ai marked with “technical questions” contact.
* Removed redundant `Maintainer:` lines.
* Minor cleanup:

    * Consistent header guards and `#endif` formatting.
    * Fixed trailing newlines.

### ✅ Tests

* **`tests/CMakeLists.txt`**:

    * Variant-aware test build with unified CMake.
    * Defines main executable: `test_document_builder`.
    * Coverage aggregation with `llvm-profdata` + `llvm-cov` (HTML + console).
* **`tests/build.sh`**:

    * Supports variants (`debug|memory|static|shared|coverage`).
    * Auto job detection for parallel builds.
* Test sources (`test_document_builder.c`, `get_context.c`):

    * SPDX/licensing headers updated.
    * Code unchanged except for cleanup (consistent endings).

---

## Impact

* 🚀 Streamlined builds with one script and modern CMake variants.
* 🛡️ Consistent SPDX headers and simplified NOTICE.
* 📖 Developer docs clarified with explicit dependency setup.
* ✅ Stronger, variant-aware test and coverage workflow.
