# Injects FW_VERSION as a build flag from the git tag, instead of a
# hand-maintained version file - see CLAUDE.md's "OTA + versioning plan".
# The tag is the single source of truth: `git describe --tags --match
# "fw-v*"` runs on every build, so releasing is just tagging a commit
# `fw-vX.Y.Z` and pushing it - nothing to hand-edit, so the compiled
# version can never drift from the tag.
#
# A build made between tags (or before any fw-v* tag exists at all) still
# gets a usable, traceable version string rather than failing: `--always`
# falls back to the abbreviated commit hash, and `--dirty` appends
# "-dirty" if the working tree has uncommitted changes. e.g.:
#   fw-v1.3.0            (exact tag)            -> "1.3.0"
#   fw-v1.2.0-14-gabc1234 (14 commits past a tag) -> "1.2.0-14-gabc1234"
#   6e17c50               (no fw-v* tag yet)      -> "6e17c50"
Import("env")

import subprocess


def get_fw_version():
    project_dir = env.subst("$PROJECT_DIR")
    try:
        raw = subprocess.check_output(
            ["git", "describe", "--tags", "--match", "fw-v*", "--dirty", "--always"],
            cwd=project_dir,
            stderr=subprocess.DEVNULL,
        ).decode().strip()
    except Exception:
        # No git binary, not a git checkout (e.g. a source zip), etc. - never
        # fail the build over a cosmetic version string.
        return "unknown"
    # A real tag match starts with the "fw-v" prefix (see --match above);
    # strip it so the displayed/compared version is a plain "1.3.0", not
    # "fw-v1.3.0". The no-tag commit-hash fallback has no such prefix and
    # is left as-is.
    if raw.startswith("fw-v"):
        raw = raw[len("fw-v"):]
    return raw


fw_version = get_fw_version()
print("Firmware version (from git): %s" % fw_version)
env.Append(BUILD_FLAGS=['-DFW_VERSION=\\"%s\\"' % fw_version])
