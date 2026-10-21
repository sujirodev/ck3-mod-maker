# Nexus Mods page helper for the shared release workflow (Nexus Mods API v3).
#
#   resolve  Print the unique mod id for a game-scoped id (the number in the
#            mod page URL). The upload action and the edit endpoint need it.
#   update   Replace the page summary and full description (BBCode) with the
#            contents of the given files.
#
# The API key is read from the NEXUSMODS_API_KEY environment variable.
# Usage:
#   python tools/nexus_page.py resolve --game crusaderkings3 --game-mod-id 123
#   python tools/nexus_page.py update --mod-id <unique id> \
#       --summary docs/nexus_summary.txt --description docs/nexus_description.bbcode
import argparse
import json
import os
import sys
import urllib.error
import urllib.request

API = os.environ.get("NEXUSMODS_API_BASE", "https://api.nexusmods.com/v3")


def call(method, path, body=None):
    key = os.environ.get("NEXUSMODS_API_KEY")
    if not key:
        sys.exit("NEXUSMODS_API_KEY is not set")
    data = json.dumps(body).encode("utf-8") if body is not None else None
    req = urllib.request.Request(API + path, data=data, method=method)
    req.add_header("apikey", key)
    req.add_header("Accept", "application/json")
    req.add_header("User-Agent", "ck3-mod-maker-release")
    if data is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            raw = resp.read()
            return json.loads(raw) if raw else None
    except urllib.error.HTTPError as e:
        sys.exit(f"{method} {path} failed: {e.code} {e.read().decode('utf-8', 'replace')}")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("resolve")
    r.add_argument("--game", required=True)
    r.add_argument("--game-mod-id", required=True)
    u = sub.add_parser("update")
    u.add_argument("--mod-id", required=True)
    u.add_argument("--summary", required=True)
    u.add_argument("--description", required=True)
    args = ap.parse_args()

    if args.cmd == "resolve":
        mod = call("GET", f"/games/{args.game}/mods/{args.game_mod_id}")["data"]
        print(mod["id"])
        return

    summary = " ".join(open(args.summary, encoding="utf-8").read().split())
    description = open(args.description, encoding="utf-8").read().strip()
    if not 1 <= len(summary) <= 350:
        sys.exit(f"summary must be 1-350 characters, got {len(summary)}")
    if not description:
        sys.exit("description is empty")
    call("PATCH", f"/mods/{args.mod_id}", {"summary": summary, "description": description})
    print(f"updated mod {args.mod_id}: summary {len(summary)} chars, description {len(description)} chars")


if __name__ == "__main__":
    main()
