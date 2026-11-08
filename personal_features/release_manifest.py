"""Stage a model-specific signed application release for the personal updater."""

import argparse
import getpass
import hashlib
import re
from pathlib import Path
from tempfile import TemporaryDirectory

from cryptography.exceptions import UnsupportedAlgorithm
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

MODELS = {"x3": 5, "x4pro": 9}  # esp_image_header_t chip_id
MAX_APPLICATION = 6_553_600
VERSION = re.compile(r"(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)(?:-rc\.[1-9]\d*)?", re.ASCII)


def release_body(image, model, version):
    if len(version) > 30 or not VERSION.fullmatch(version):
        raise ValueError("Use a numeric version or an explicit -rc.N testing version")
    if any(int(part) > 0xFFFFFFFF for part in re.split(r"\.|-rc\.", version)):
        raise ValueError("Release version component exceeds the device limit")
    if not 24 <= len(image) <= MAX_APPLICATION or image[0] != 0xE9:
        raise ValueError("Expected an ESP application image")
    if int.from_bytes(image[12:14], "little") != MODELS[model]:
        raise ValueError("Image chip does not match the selected model")
    tags = set(re.findall(rb"CROSSPOINT-BOARD-V1:([a-z0-9_]+);", image))
    if tags != {model.encode()}:
        raise ValueError("Image board tag does not match the selected model")
    if f"!XTEINK-PERSONAL-BUILD:{model}:{version}\0".encode() not in image:
        raise ValueError("Image version does not match the release")
    digest = hashlib.sha256(image).hexdigest()
    return f"XTEINK-PERSONAL-OTA-1\n{model}\n{version}\n{len(image)}\n{digest}\n".encode()


def sign(body, key):
    with key.open("rb") as source:
        pem = source.read(8193)
    if len(pem) > 8192:
        raise ValueError("Release key file exceeds the PEM size limit")
    try:
        private = serialization.load_pem_private_key(pem, password=None)
    except TypeError:
        password = getpass.getpass("Release key passphrase: ").encode()
        private = serialization.load_pem_private_key(pem, password=password)
    if not isinstance(private, ec.EllipticCurvePrivateKey) or not isinstance(private.curve, ec.SECP256R1):
        raise ValueError("Use a P-256 ECDSA release key")
    signature = private.sign(body, ec.ECDSA(hashes.SHA256()))
    if not 8 <= len(signature) <= 80:
        raise ValueError("Unexpected ECDSA signature size")
    return signature


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", choices=MODELS, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--application", type=Path, required=True)
    parser.add_argument("--private-key", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        key = args.private_key.resolve(strict=True)
        source_root = Path(__file__).resolve().parents[1]
        if key.is_relative_to(source_root):
            raise ValueError("Keep the private signing key outside the firmware source tree")
        if args.output.exists():
            raise FileExistsError("Choose a new release staging directory")
        with args.application.open("rb") as app:
            image = app.read(MAX_APPLICATION + 1)
        body = release_body(image, args.model, args.version)
        signature = sign(body, key)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with TemporaryDirectory(prefix="release-", dir=args.output.parent) as tmp:
            stage = Path(tmp) / "assets"
            stage.mkdir()
            (stage / f"personal-{args.version}-{args.model}.bin").write_bytes(image)
            (stage / f"personal-{args.model}.manifest").write_bytes(body + signature.hex().encode() + b"\n")
            stage.rename(args.output)
    except (OSError, ValueError, TypeError, UnsupportedAlgorithm) as error:
        parser.exit(1, f"Release staging failed: {error}\n")


if __name__ == "__main__":
    main()
