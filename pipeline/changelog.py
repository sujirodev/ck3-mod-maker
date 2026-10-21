# Prints the CHANGELOG.md section for one version.
#   --format md    the section body as Markdown (GitHub Release notes)
#   --format text  one plain line per entry, no Markdown (Nexus Mods changelog)
# Exits with an error if the version has no section or no entries.
# Usage: python pipeline/changelog.py [--mod <mod repo>] 1.0.1 [--format md|text]
import argparse
import re
import sys
from pathlib import Path


def section(root, version):
    text = (Path(root) / "CHANGELOG.md").read_text(encoding="utf-8")
    m = re.search(rf"^## \[{re.escape(version)}\][^\n]*\n(.*?)(?=^## |\Z)", text, re.M | re.S)
    if not m:
        sys.exit(f"CHANGELOG.md has no section for {version}")
    body = m.group(1).strip()
    entries = [l[2:].strip() for l in body.splitlines() if l.startswith("- ")]
    if not entries:
        sys.exit(f"CHANGELOG.md section {version} has no '- ' entries")
    return body, entries


def plain(line):
    line = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", line)
    return re.sub(r"[*_`]", "", line)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mod", default=".")
    ap.add_argument("version")
    ap.add_argument("--format", choices=["md", "text"], default="md")
    args = ap.parse_args()
    body, entries = section(args.mod, args.version.removeprefix("v"))
    if args.format == "md":
        print(body)
    else:
        print("\n".join(plain(e) for e in entries))


if __name__ == "__main__":
    main()
