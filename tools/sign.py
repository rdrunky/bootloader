import argparse
import os
import pathlib
import sys

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

SIGNATURE_SUFFIX = ".sig"


def load(path):
    key = serialization.load_pem_private_key(pathlib.Path(path).read_bytes(), password=None)
    if not isinstance(key, Ed25519PrivateKey):
        sys.exit(f"{path}: not an ed25519 private key")
    return key


def keygen(options):
    key = Ed25519PrivateKey.generate()
    pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                            serialization.NoEncryption())
    descriptor = os.open(options.key, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "wb") as output:
        output.write(pem)


def public(options):
    key = load(options.key).public_key()
    raw = key.public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    pathlib.Path(options.output).write_bytes(raw)


def sign(options):
    key = load(options.key)
    for name in options.files:
        path = pathlib.Path(name)
        signature = key.sign(path.read_bytes())
        path.with_name(path.name + SIGNATURE_SUFFIX).write_bytes(signature)


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)

    command = commands.add_parser("keygen", help="create a new private key")
    command.add_argument("key")
    command.set_defaults(run=keygen)

    command = commands.add_parser("public", help="write the raw public key")
    command.add_argument("key")
    command.add_argument("output")
    command.set_defaults(run=public)

    command = commands.add_parser("sign", help="write a detached signature next to each file")
    command.add_argument("key")
    command.add_argument("files", nargs="+")
    command.set_defaults(run=sign)

    options = parser.parse_args()
    options.run(options)


if __name__ == "__main__":
    main()
