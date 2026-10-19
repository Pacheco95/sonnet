#!/usr/bin/env python3
"""Downloads the Khronos assets of the TAA harness's project, apps/samples/taa (docs/rendering.md, "TAA checks").

ChronographWatch and TransmissionTest come from glTF-Sample-Assets and the helipad environment from
glTF-Sample-Environments, each at a pinned commit. They are not committed (size and licences: see each
model's LICENSE.txt under assets/); the files land in git-ignored folders and get the sidecars with the
fixed identities that scenes/watch.scene.json and scenes/transmission_test.scene.json refer to.

Needs git and network access. Run from the repository root; running it again replaces the files.
"""
import json
import shutil
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_taa_fixtures as fixtures  # noqa: E402

SAMPLE_ASSETS = ("https://github.com/KhronosGroup/glTF-Sample-Assets", "edc7c9e67c639d230715049ee31f9a96a6babbbe")
SAMPLE_ENVIRONMENTS = ("https://github.com/KhronosGroup/glTF-Sample-Environments",
                       "4fc29557763275b8519b42920f85ad0ca1822f22")
MODELS = ["ChronographWatch", "CommercialRefrigerator", "TransmissionTest"]


def run(*command, cwd=None):
    subprocess.run(command, cwd=cwd, check=True)


def main():
    assets = fixtures.ASSETS
    assets.mkdir(parents=True, exist_ok=True)
    url, revision = SAMPLE_ASSETS
    with tempfile.TemporaryDirectory() as temporary:
        checkout = Path(temporary) / "assets"
        run("git", "clone", "--filter=blob:none", "--no-checkout", url, str(checkout))
        run("git", "sparse-checkout", "set", *[f"Models/{name}/glTF" for name in MODELS], cwd=checkout)
        run("git", "checkout", revision, cwd=checkout)
        for name in MODELS:
            target = assets / name
            shutil.rmtree(target, ignore_errors=True)
            shutil.copytree(checkout / "Models" / name / "glTF", target)
            shutil.copy(checkout / "Models" / name / "LICENSE.md", target / "LICENSE.txt")  # .txt: its links are the repository's
            sidecar = {"version": fixtures.SIDECAR_VERSION, "uuid": fixtures.identity(f"{name}.gltf"), "settings": {}}
            (target / f"{name}.gltf.meta").write_text(json.dumps(sidecar, indent=2) + "\n")
    url, revision = SAMPLE_ENVIRONMENTS
    # The environments are Git LFS objects, which raw.githubusercontent.com serves as pointer files.
    media = url.replace("github.com", "media.githubusercontent.com/media") + f"/{revision}/helipad.hdr"
    with urllib.request.urlopen(media) as response:
        (assets / "helipad.hdr").write_bytes(response.read())
    sidecar = {"version": fixtures.SIDECAR_VERSION, "uuid": fixtures.identity("helipad.hdr"), "settings": {}}
    (assets / "helipad.hdr.meta").write_text(json.dumps(sidecar, indent=2) + "\n")
    print(f"fetched {', '.join(MODELS)} and helipad.hdr into {assets}")


if __name__ == "__main__":
    main()
