import argparse
import hashlib
import pathlib
import re

TOKEN = re.compile(r"\{sha256:([^}]+)\}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("template")
    parser.add_argument("root")
    parser.add_argument("output")
    options = parser.parse_args()

    root = pathlib.Path(options.root)
    text = pathlib.Path(options.template).read_text()
    text = TOKEN.sub(lambda match: hashlib.sha256((root / match.group(1)).read_bytes()).hexdigest(), text)
    pathlib.Path(options.output).write_text(text)


if __name__ == "__main__":
    main()
