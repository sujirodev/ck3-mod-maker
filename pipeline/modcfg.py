# Reads a mod repository's ck3mod.toml (shared by package.py, changelog.py and the release workflow).
#
#   [mod]
#   dir = "my_mod"                 # folder name under Documents/.../mod/ and inside the zip
#   title = "My Mod"               # release and Nexus display name
#   include = ["descriptor.mod", "common", ...]   # optional, what goes into the zip
#   exclude = "teste|test|debug"   # optional regex on file names left out of the zip
import re
import sys
import tomllib
from pathlib import Path

DEFAULT_INCLUDE = ["descriptor.mod", "README.md", "LICENSE", "thumbnail.png", "common", "events", "gfx", "gui",
                   "history", "localization", "map_data", "music", "sound", "plugin"]
DEFAULT_EXCLUDE = r"(teste|test|debug)"


def load(mod_root):
    root = Path(mod_root).resolve()
    p = root / "ck3mod.toml"
    if not p.exists():
        sys.exit(f"{p} not found")
    cfg = tomllib.loads(p.read_text(encoding="utf-8")).get("mod", {})
    for key in ("dir", "title"):
        if not cfg.get(key):
            sys.exit(f"ck3mod.toml: [mod] {key} is required")
    cfg["root"] = root
    cfg["include"] = cfg.get("include", DEFAULT_INCLUDE)
    cfg["exclude"] = re.compile(cfg.get("exclude", DEFAULT_EXCLUDE), re.I)
    return cfg


def descriptor_field(root, key):
    text = (Path(root) / "descriptor.mod").read_text(encoding="utf-8-sig")
    m = re.search(rf'^{key}="([^"]*)"', text, re.M)
    if not m:
        sys.exit(f"descriptor.mod has no {key}")
    return m.group(1)
