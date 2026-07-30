#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

DRIVER = "goodix5125"
VERSION = "1.94.10"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def patch_top(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    if "    'goodix5125',\n" not in text:
        text = replace_once(
            text,
            "    'goodixmoc',\n",
            "    'goodixmoc',\n    'goodix5125',\n",
            "default driver list",
        )
    if "    'goodix5125' : [ 'openssl' ],\n" not in text:
        text = replace_once(
            text,
            "    'uru4000' : [ 'openssl' ],\n",
            "    'uru4000' : [ 'openssl' ],\n    'goodix5125' : [ 'openssl' ],\n",
            "driver dependency map",
        )
    path.write_text(text, encoding="utf-8")


def patch_lib(path: Path, native_root: Path) -> None:
    text = path.read_text(encoding="utf-8")
    driver_entry = "    'goodix5125' :\n        [ 'drivers/goodix5125.c' ],\n"
    if driver_entry not in text:
        text = replace_once(
            text,
            "    'goodixmoc' :\n        [ 'drivers/goodixmoc/goodix.c', 'drivers/goodixmoc/goodix_proto.c' ],\n",
            "    'goodixmoc' :\n        [ 'drivers/goodixmoc/goodix.c', 'drivers/goodixmoc/goodix_proto.c' ],\n"
            + driver_entry,
            "driver source map",
        )

    root = native_root.resolve()
    include_path = (root / "include").as_posix()
    matcher_archive = (root / "build" / "libgx5125matcher.a").as_posix()
    enrollment_archive = (root / "build" / "libgx5125enrollment.a").as_posix()
    pipeline_archive = (root / "build" / "libgx5125pipeline.a").as_posix()
    block = f"""
goodix5125_deps = []
if 'goodix5125' in drivers
    goodix5125_deps += declare_dependency(
        include_directories: include_directories('{include_path}'),
        dependencies: [
            dependency('libusb-1.0'),
            dependency('threads'),
        ],
        link_args: [
            '-Wl,--start-group',
            '{matcher_archive}',
            '{enrollment_archive}',
            '{pipeline_archive}',
            '-Wl,--end-group',
        ],
    )
endif

"""
    if "goodix5125_deps = []" not in text:
        text = replace_once(text, "deps = [\n", block + "deps = [\n", "dependency block")
        text = replace_once(
            text,
            "] + optional_deps\n",
            "] + optional_deps + goodix5125_deps\n",
            "dependency list tail",
        )
    path.write_text(text, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("native_root", type=Path)
    args = parser.parse_args()

    source = args.source.resolve()
    native_root = args.native_root.resolve()
    required = [
        source / "meson.build",
        source / "libfprint" / "meson.build",
        native_root / "include" / "gx5125" / "pipeline.h",
        native_root / "include" / "gx5125" / "enrollment.h",
        native_root / "include" / "gx5125" / "matcher.h",
        native_root / "include" / "gx5125" / "secret.h",
        native_root / "build" / "libgx5125pipeline.a",
        native_root / "build" / "libgx5125enrollment.a",
        native_root / "build" / "libgx5125matcher.a",
        native_root / "libfprint" / "goodix5125.c",
    ]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise RuntimeError("missing required files: " + ", ".join(missing))

    version_text = (source / "meson.build").read_text(encoding="utf-8")
    if f"version: '{VERSION}'" not in version_text:
        raise RuntimeError(f"unsupported libfprint source; expected {VERSION}")

    (source / "libfprint" / "drivers" / "goodix5125.c").write_bytes(
        (native_root / "libfprint" / "goodix5125.c").read_bytes()
    )
    patch_top(source / "meson.build")
    patch_lib(source / "libfprint" / "meson.build", native_root)
    print(
        "GOODIX_BETA_OVERLAY=PASS "
        "libfprint_version:1.94.10 driver:goodix5125 "
        "native_archives:3 encrypted_templates:1 automatic_touch:1"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
