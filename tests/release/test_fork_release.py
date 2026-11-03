from __future__ import annotations

import hashlib
import json
import pathlib
import tarfile
import tempfile
import unittest

from fork_release import main
from native_package_release import ARTIFACTS_NAME, sbom_path
from test_release_tools import create_archive

TAG = "terrain-3d-202611.1"
VERSION = "0.0.0-terrain-3d-202611.1"
SHA = "c" * 40
DOWNLOAD_URL = "https://github.com/example/maplibre-native-ffi/releases/download"


class ForkReleaseTest(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = pathlib.Path(temporary.name)
        self.release_dir = self.root / "release"
        self.release_dir.mkdir()
        self.presets = ["linux-gnu-x64-egl", "linux-gnu-x64-vulkan"]
        for preset, backend in zip(self.presets, ["opengl", "vulkan"], strict=True):
            create_archive(self.archive(preset), preset, backend)

    def archive(self, preset: str) -> pathlib.Path:
        return self.release_dir / f"maplibre-native-c-{preset}.tar.gz"

    def prepare(self, *extra: str) -> None:
        main(
            [
                str(self.release_dir),
                TAG,
                VERSION,
                *self.presets,
                "--sha",
                SHA,
                "--download-url",
                f"{DOWNLOAD_URL}/",
                *extra,
            ]
        )

    def test_pins_the_named_presets_at_the_fork_release(self) -> None:
        pin = self.root / "native_release.json"
        self.prepare("--dart-pin", str(pin))

        index = json.loads((self.release_dir / ARTIFACTS_NAME).read_text())
        self.assertEqual(
            pin.read_text(), (self.release_dir / ARTIFACTS_NAME).read_text()
        )
        self.assertEqual(
            (index["tag"], index["version"], index["commit"]), (TAG, VERSION, SHA)
        )
        self.assertEqual(
            [
                (entry["backend"], entry["archive"], entry["sha256"])
                for entry in index["artifacts"]
            ],
            [
                (
                    backend,
                    self.archive(preset).name,
                    hashlib.sha256(self.archive(preset).read_bytes()).hexdigest(),
                )
                for preset, backend in zip(
                    self.presets, ["opengl", "vulkan"], strict=True
                )
            ],
        )
        sbom = index["artifacts"][0]["sbom"]
        self.assertEqual(sbom["url"], f"{DOWNLOAD_URL}/{TAG}/{sbom['file']}")
        inventory = json.loads(sbom_path(self.archive(self.presets[0])).read_text())
        self.assertEqual(
            inventory["metadata"]["component"]["externalReferences"][-1]["url"],
            f"{DOWNLOAD_URL}/{TAG}/{self.archive(self.presets[0]).name}",
        )

        root = f"maplibre-native-c-{self.presets[0]}"
        with tarfile.open(self.archive(self.presets[0]), "r:gz") as package:
            descriptor = json.load(
                package.extractfile(f"{root}/share/maplibre-native-c/artifact.json")
            )
        self.assertEqual((descriptor["version"], descriptor["commit"]), (VERSION, SHA))

        checksums = (self.release_dir / "SHA256SUMS").read_text()
        for name in [ARTIFACTS_NAME, *(self.archive(p).name for p in self.presets)]:
            self.assertIn(f"  {name}\n", checksums)

    def test_refuses_an_archive_of_another_preset(self) -> None:
        create_archive(self.archive("linux-gnu-arm64-egl"), "linux-gnu-arm64-egl")

        with self.assertRaisesRegex(SystemExit, "unexpected"):
            self.prepare()


if __name__ == "__main__":
    unittest.main()
