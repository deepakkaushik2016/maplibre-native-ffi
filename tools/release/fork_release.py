#!/usr/bin/env python3
"""Prepares the native assets of a fork release that publishes some presets.

A fork ships the archives that its applications build with, under a tag of its
own, so that a Dart package which names the fork's releases in `release_url`
downloads them. Each archive is stamped and inventoried as an upstream release
stamps it, and the artifact index lists them. The release holds no XCFramework
or Zig package.
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib

from native_package_release import (
    ARTIFACTS_NAME,
    archive_inventory,
    artifact_index,
    native_archives,
    publish_inventory,
    rewrite_archive,
    sbom_path,
    write_checksums,
    write_pin,
)


def prepare(
    release_dir: pathlib.Path,
    tag: str,
    version: str,
    sha: str,
    download_url: str,
    presets: list[str],
) -> pathlib.Path:
    """Stamps the archives of `presets` in `release_dir` for release `tag`,
    which `download_url` serves, and returns the artifact index it writes."""
    archives = native_archives(release_dir, presets)
    for archive in archives:
        rewrite_archive(archive, version, sha)
        publish_inventory(archive, archive_inventory(archive), tag, download_url)
    index = release_dir / ARTIFACTS_NAME
    index.write_text(
        json.dumps(artifact_index(tag, archives, download_url), indent=2) + "\n",
        encoding="utf-8",
    )
    write_checksums(
        release_dir, [*archives, index, *(sbom_path(archive) for archive in archives)]
    )
    return index


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("release_dir", type=pathlib.Path)
    parser.add_argument("tag")
    parser.add_argument("version")
    parser.add_argument("presets", nargs="+")
    parser.add_argument(
        "--sha",
        default=os.environ.get("PUBLISH_SHA"),
        help="The source commit; defaults to PUBLISH_SHA",
    )
    parser.add_argument(
        "--download-url",
        default=None,
        help="The base URL of the release downloads; defaults to this "
        "repository's GitHub releases",
    )
    parser.add_argument(
        "--dart-pin",
        type=pathlib.Path,
        help="Also pin the release in this Dart package's build hook",
    )
    args = parser.parse_args(arguments)
    if not args.sha:
        parser.error("--sha or PUBLISH_SHA is required")
    download_url = args.download_url
    if download_url is None:
        repository = os.environ.get("GITHUB_REPOSITORY")
        if not repository:
            parser.error("--download-url or GITHUB_REPOSITORY is required")
        server = os.environ.get("GITHUB_SERVER_URL", "https://github.com")
        download_url = f"{server}/{repository}/releases/download"
    index = prepare(
        args.release_dir,
        args.tag,
        args.version,
        args.sha,
        download_url.rstrip("/"),
        args.presets,
    )
    if args.dart_pin is not None:
        write_pin(args.dart_pin, args.tag, index)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
