# Builds a mod's distributable zip (from the guardian_calc_assistant release tooling, made generic):
#   <dir>.mod          launcher entry, relative path
#   <dir>/...          mod files listed in ck3mod.toml `include` (no tools, docs or git files)
# Extract it into Documents/Paradox Interactive/Crusader Kings III/mod/.
#
# Usage: python pipeline/package.py --mod <mod repo> [--tag vX.Y.Z] [--out dist]
#        python pipeline/package.py --mod <mod repo> --info     (dir=, title=, version= lines for GITHUB_OUTPUT)
# With --tag, fails unless the tag matches the descriptor.mod version.
import argparse
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import modcfg  # noqa: E402

# Fixed timestamp so identical sources give identical zips.
STAMP = (2020, 1, 1, 0, 0, 0)


def add(zf, arcname, data):
    info = zipfile.ZipInfo(arcname, STAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o644 << 16
    zf.writestr(info, data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mod", default=".")
    ap.add_argument("--tag")
    ap.add_argument("--out", default="dist")
    ap.add_argument("--info", action="store_true")
    args = ap.parse_args()

    cfg = modcfg.load(args.mod)
    root, mod_dir = cfg["root"], cfg["dir"]
    version = modcfg.descriptor_field(root, "version")
    if args.info:
        print(f"dir={mod_dir}\ntitle={cfg['title']}\nversion={version}")
        return
    if args.tag and args.tag.removeprefix("v") != version:
        sys.exit(f"tag {args.tag} does not match descriptor.mod version {version}")

    # The launcher entry is the descriptor plus a path relative to the game's user folder.
    descriptor = (root / "descriptor.mod").read_text(encoding="utf-8-sig")
    launcher_entry = descriptor.rstrip("\n") + f'\npath="mod/{mod_dir}"\n'

    files = []
    for name in cfg["include"]:
        p = root / name
        if p.is_file():
            files.append(p)
        elif p.is_dir():
            files.extend(f for f in p.rglob("*") if f.is_file() and not cfg["exclude"].search(f.name))
    files.sort(key=lambda f: f.relative_to(root).as_posix())

    out = Path(args.out)
    out = out if out.is_absolute() else root / out
    out.mkdir(parents=True, exist_ok=True)
    zip_path = out / f"{mod_dir}-{version}.zip"
    with zipfile.ZipFile(zip_path, "w") as zf:
        add(zf, f"{mod_dir}.mod", launcher_entry.encode("utf-8"))
        for f in files:
            add(zf, f"{mod_dir}/{f.relative_to(root).as_posix()}", f.read_bytes())
    print(zip_path)


if __name__ == "__main__":
    main()
