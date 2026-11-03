#!/usr/bin/env python3
"""Prepares, and verifies, the native assets of a fork release that publishes
some presets.

A fork ships the archives that its applications build with, under a tag of its
own, so that a Dart package which names the fork's releases in `release_url`
downloads them. Each archive is stamped and inventoried as an upstream release
stamps it, and the artifact index lists them. The release holds no XCFramework
or Zig package.

`prepare` stamps the assets and can write the Dart pin. `verify` checks that
assets are the ones a committed pin names, so that a release publishes only
archives its pin can download.
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
    sha256,
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


def verify(release_dir: pathlib.Path, tag: str, pin: pathlib.Path) -> None:
    """Exits unless `release_dir` holds exactly the assets of release `tag`
    that `pin` names, each with the digest the pin records."""
    pinned = json.loads(pin.read_text(encoding="utf-8"))
    problems = []
    if pinned["tag"] != tag:
        problems.append(f"the pin names the release {pinned['tag']}, not {tag}")
    index = release_dir / ARTIFACTS_NAME
    if not index.is_file() or json.loads(index.read_text(encoding="utf-8")) != pinned:
        problems.append(f"{ARTIFACTS_NAME} differs from the pin")
    expected = {}
    for artifact in pinned["artifacts"]:
        expected[artifact["archive"]] = artifact["sha256"]
        expected[artifact["sbom"]["file"]] = artifact["sbom"]["sha256"]
    for name, digest in sorted(expected.items()):
        path = release_dir / name
        if not path.is_file():
            problems.append(f"{name} is missing")
        elif sha256(path) != digest:
            problems.append(f"{name} differs from the pin")
    for path in sorted(release_dir.glob("*.tar.gz")) + sorted(
        release_dir.glob("*.cdx.json")
    ):
        if path.name not in expected:
            problems.append(f"{path.name} is not in the pin")
    if problems:
        raise SystemExit(f"the release does not match its pin: {'; '.join(problems)}")


def main(arguments: list[str] | None = None) -> int:
    cli = argparse.ArgumentParser()
    commands = cli.add_subparsers(dest="command", required=True)
    checking = commands.add_parser(
        "verify", help="Check the assets against a committed Dart pin"
    )
    checking.add_argument("release_dir", type=pathlib.Path)
    checking.add_argument("tag")
    checking.add_argument("pin", type=pathlib.Path)
    parser = commands.add_parser("prepare", help="Stamp the assets of a release")
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
    args = cli.parse_args(arguments)
    if args.command == "verify":
        verify(args.release_dir, args.tag, args.pin)
        return 0
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
