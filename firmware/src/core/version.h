#pragma once

// FW_VERSION is injected at build time by extra_script.py from the git tag
// (`git describe --tags --match "fw-v*"`) - see CLAUDE.md's "OTA +
// versioning plan". There is no hand-maintained version string: bumping
// the version is just tagging a commit `fw-vX.Y.Z` and pushing it, nothing
// to edit here. The #ifndef fallback below only matters if this file is
// ever compiled outside PlatformIO's normal build (e.g. straight from the
// Arduino IDE, which wouldn't run extra_script.py) - it just avoids a
// missing-macro compile error, not a real version.
#ifndef FW_VERSION
#define FW_VERSION "unknown"
#endif
