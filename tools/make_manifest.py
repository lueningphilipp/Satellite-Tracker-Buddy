#!/usr/bin/env python3
"""Package a compiled firmware .bin + its OTA manifest for a release.

Used by .github/workflows/release.yml, and by hand for testing the device's
OTA flow against a local web server before a release exists (or while the
repo is still private, when GitHub's download URLs 404 for the device):

    cd firmware && pio run
    python ../tools/make_manifest.py --version 9.9.9 \
        --bin .pio/build/epaper/firmware.bin \
        --url http://192.168.1.50:8000/firmware-9.9.9.bin --out /tmp/ota
    python -m http.server -d /tmp/ota 8000
    # then set the config page's "Update manifest URL" to
    # http://192.168.1.50:8000/manifest.json and click "Check for updates"

Output (in --out):
    firmware-<version>.bin   copy of the binary under its release name
    manifest.json            {"version","url","sha256","size","tag"}

The manifest's "url" must be the *tag-specific* asset URL, never GitHub's
"latest" alias - the device pairs this manifest's sha256 with exactly that
binary, and a "latest" URL could start pointing at a newer release between
the manifest fetch and the download.

Also sanity-checks that the version string is actually embedded in the
binary (FW_VERSION is a string literal in .rodata), which catches a build
made from a checkout whose `git describe` didn't see the tag - the device
would otherwise report the wrong version after installing it.
"""

import argparse
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

SEMVER = re.compile(r"^\d+\.\d+\.\d+$")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", required=True, help="plain x.y.z (the fw-v tag without its prefix)")
    ap.add_argument("--bin", required=True, type=Path, help="compiled firmware.bin from `pio run`")
    ap.add_argument("--url", required=True, help="public download URL the renamed .bin will live at")
    ap.add_argument("--out", required=True, type=Path, help="output directory (created if missing)")
    ap.add_argument(
        "--skip-embedded-check",
        action="store_true",
        help="don't require the version string to appear inside the binary (local testing "
        "with a made-up version number)",
    )
    args = ap.parse_args()

    if not SEMVER.match(args.version):
        print(f"error: version '{args.version}' is not plain x.y.z", file=sys.stderr)
        return 1
    if not args.bin.is_file():
        print(f"error: {args.bin} not found - run `pio run` first", file=sys.stderr)
        return 1
    if not (args.url.startswith("https://") or args.url.startswith("http://")):
        print("error: --url must start with http:// or https://", file=sys.stderr)
        return 1

    data = args.bin.read_bytes()
    if not args.skip_embedded_check and args.version.encode() not in data:
        print(
            f"error: '{args.version}' does not appear inside {args.bin} - the binary was "
            "compiled with a different FW_VERSION (tag not visible to `git describe`?). "
            "Pass --skip-embedded-check only for local testing.",
            file=sys.stderr,
        )
        return 1

    args.out.mkdir(parents=True, exist_ok=True)
    bin_name = f"firmware-{args.version}.bin"
    expected_name = args.url.rsplit("/", 1)[-1]
    if expected_name != bin_name:
        print(
            f"error: --url ends in '{expected_name}' but the binary will be named '{bin_name}'",
            file=sys.stderr,
        )
        return 1
    shutil.copyfile(args.bin, args.out / bin_name)

    manifest = {
        "version": args.version,
        "url": args.url,
        "sha256": hashlib.sha256(data).hexdigest(),
        "size": len(data),
        "tag": f"fw-v{args.version}",
    }
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"wrote {args.out / bin_name} ({len(data)} bytes) and {args.out / 'manifest.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
