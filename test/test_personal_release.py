"""Run production release parsing/comparison on a C++20 host and test signing.

Run with a configured C++ compiler (CXX or cl.exe) and Python cryptography.
These checks do not replace an actual device download, restart and recovery.
"""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("release_manifest", ROOT / "personal_features/release_manifest.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)

HARNESS = r'''
#include "PersonalRelease.h"
#include <iostream>
#include <string>
using namespace personal_release;
int checks = 0;
int failures = 0;
void check(bool value, const char* description) {
  ++checks;
  if (!value) { ++failures; std::cerr << description << "\n"; }
}
int main() {
  Version v;
  check(parseVersion("0.9.0-rc.2", v, true) && v.releaseCandidate == 2, "RC parsed");
  check(parseVersion("1.0.0", v, true) && v.releaseCandidate == 0, "stable parsed");
  for (auto bad : {"", "1.2", "1.2.3.4", "01.2.3", "+1.2.3", "1.2.3-rc.0",
                   "1.2.3-rc.01", "1.2.3-rc.-1", "1.2.3-rc.4294967296", "4294967296.0.0",
                   "1.2.3-rc.2x", "1.2.3-rc.2\n", "1.2.3-beta.1"}) {
    check(!parseVersion(bad, v, true), bad);
  }
  check(!parseVersion("1.0.0-rc.1", v, false), "stable rejects RC");
  check(newerRelease("0.9.0-rc.3", "0.9.0-rc.2", true), "increment RC");
  check(newerRelease("0.9.0-rc.10", "0.9.0-rc.9", true), "numeric RC order");
  check(!newerRelease("0.9.0-rc.2", "0.9.0-rc.2", true), "same RC rejected");
  check(!newerRelease("0.9.0-rc.1", "0.9.0-rc.2", true), "older RC rejected");
  check(!newerRelease("0.8.9-rc.99", "0.9.0-rc.2", true), "older triplet rejected");
  check(newerRelease("0.9.1-rc.1", "0.9.0-rc.99", true), "new triplet accepted");
  check(newerRelease("1.0.0", "0.9.0-rc.3", true), "test to stable");
  check(newerRelease("1.0.0", "1.0.0-rc.3", true), "same triplet graduation");
  check(!newerRelease("1.0.0-rc.99", "1.0.0", true), "stable cannot downgrade to RC");
  check(!newerRelease("1.1.0-rc.1", "1.0.0", false), "stable channel rejects future RC");
  check(newerRelease("1.0.1", "1.0.0", false), "stable patch accepted");
  check(!newerRelease("1.0.0", "invalid", true), "invalid installed version rejected");
  check(validRepository(repository), "publisher valid");
  check(!validRepository("a/b/c") && !validRepository("../b"), "invalid publisher rejected");
  check(testingChannel == bool(EXPECT_TESTING), "build chooses channel");
  check(std::string_view(manifestRoute) == (testingChannel ? "download/crossloop-testing/" : "latest/download/"),
        "test feed bypasses stable latest");
  auto manifest = [](const std::string& board, const std::string& ver) {
    return std::string(magic) + "\n" + board + "\n" + ver + "\n24\n" +
           std::string(64, 'a') + "\n" + std::string(16, 'b') + "\n";
  };
  Manifest parsed;
  auto body = manifest(model, "1.0.0");
  check(parseManifest(body, model, parsed) && parsed.size == 24, "stable manifest parsed");
  check(body.substr(0, parsed.signedSize).ends_with(std::string(64, 'a') + "\n"), "signature boundary");
  auto rc = manifest(model, "0.9.0-rc.3");
  check(parseManifest(rc, model, parsed) == testingChannel, "manifest channel enforced");
  check(!parseManifest(body, std::string_view(model) == "x3" ? "x4pro" : "x3", parsed), "wrong model rejected");
  check(!parseManifest(body.substr(0, body.size()-1), model, parsed), "missing newline rejected");
  check(!parseManifest(body + "extra\n", model, parsed), "extra lines rejected");
  check(!parseManifest(std::string(513,'x'), model, parsed), "oversize rejected");
  auto marker = std::string(identityPrefix) + "0.9.0-rc.3";
  marker.push_back('\0');
  for (size_t split=0; split<=marker.size(); ++split) {
    IdentityScanner scanner(marker);
    scanner.feed(reinterpret_cast<const uint8_t*>(marker.data()), split);
    scanner.feed(reinterpret_cast<const uint8_t*>(marker.data()+split), marker.size()-split);
    check(scanner.matches(), "split identity recognized");
  }
  IdentityScanner scanner(marker);
  auto wrong = std::string(identityPrefix) + "0.9.0-rc.2";
  wrong.push_back('\0');
  scanner.feed(reinterpret_cast<const uint8_t*>(wrong.data()), wrong.size());
  check(!scanner.matches(), "wrong embedded version rejected");
  std::cout << model << " " << (testingChannel ? "testing" : "stable") << ": "
            << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
'''


def image(model="x4pro", version="0.9.0-rc.3"):
    data = bytearray(24)
    data[0] = 0xE9
    data[12:14] = release.MODELS[model].to_bytes(2, "little")
    return bytes(data) + f"CROSSPOINT-BOARD-V1:{model};!XTEINK-PERSONAL-BUILD:{model}:{version}\0".encode()


class SigningTests(unittest.TestCase):
    def test_signed_models_and_tampering(self):
        key = ec.generate_private_key(ec.SECP256R1())
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "key.pem"
            path.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                              serialization.NoEncryption()))
            for model in release.MODELS:
                for version in ("0.9.0-rc.3", "1.0.0"):
                    body = release.release_body(image(model, version), model, version)
                    signature = release.sign(body, path)
                    key.public_key().verify(signature, body, ec.ECDSA(hashes.SHA256()))
                    with self.assertRaises(InvalidSignature):
                        key.public_key().verify(signature, body.replace(model.encode(), b"wrong"), ec.ECDSA(hashes.SHA256()))
                    with self.assertRaises(InvalidSignature):
                        ec.generate_private_key(ec.SECP256R1()).public_key().verify(signature, body, ec.ECDSA(hashes.SHA256()))

    def test_bad_versions(self):
        for version in ("0.9", "01.0.0", "0.9.0-rc.0", "0.9.0-rc.01", "0.9.0-rc.4294967296", "4294967296.0.0", "0.9.0-beta.1"):
            with self.subTest(version=version), self.assertRaises(ValueError):
                release.release_body(image(version=version), "x4pro", version)

    def test_mismatched_image(self):
        for data, model, version in ((image(), "x3", "0.9.0-rc.3"), (image(), "x4pro", "0.9.0-rc.4"),
                                     (image() + b"CROSSPOINT-BOARD-V1:x3;", "x4pro", "0.9.0-rc.3"),
                                     (b"", "x4pro", "0.9.0-rc.3")):
            with self.assertRaises(ValueError):
                release.release_body(data, model, version)


def run_host():
    compiler = os.environ.get("CXX") or shutil.which("cl.exe") or shutil.which("c++")
    if not compiler:
        raise SystemExit("C++20 compiler required; configure the developer environment")
    msvc = Path(compiler).name.lower() == "cl.exe"
    with tempfile.TemporaryDirectory(prefix="crossloop-ota-test-") as tmp:
        temp = Path(tmp)
        source = temp / "release_test.cpp"
        source.write_text(HARNESS, encoding="utf-8")
        for model in ("x3", "x4pro"):
            for testing in (False, True):
                exe = temp / f"{model}-{testing}.exe"
                flags = [f"FREEINK_DEVICE_X4PRO={int(model == 'x4pro')}", f"EXPECT_TESTING={int(testing)}"]
                if not testing:
                    flags.append('CROSSLOOP_RELEASE_VERSION="1.0.0"')
                if msvc:
                    cmd = [compiler, "/nologo", "/std:c++20", "/EHsc", "/W4", *["/D"+f for f in flags],
                           "/I"+str(ROOT / "src/network"), str(source), "/Fe:"+str(exe)]
                else:
                    cmd = [compiler, "-std=c++20", "-Wall", "-Wextra", *["-D"+f for f in flags],
                           "-I"+str(ROOT / "src/network"), str(source), "-o", str(exe)]
                subprocess.run(cmd, cwd=temp, check=True)
                subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    run_host()
    unittest.main()
