# Hook PostToolUse (Edit/Write): confere o arquivo de mod recem-editado e devolve os problemas ao Claude (saida 2).
import json, os, sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from ck3ctl import config, validate  # noqa: E402


def main():
    try:
        data = json.load(sys.stdin)
    except ValueError:
        return 0
    p = (data.get('tool_input') or {}).get('file_path') or ''
    if not p or not os.path.exists(p):
        return 0
    norm = os.path.abspath(p).lower()
    alvos = [config.TEMPLATES, config.MOD_DIR]
    if not any(norm.startswith(os.path.abspath(a).lower() + os.sep) for a in alvos):
        return 0
    errs = validate.check_file(p)
    if not errs:
        return 0
    print('ck3ctl check %s:' % p, file=sys.stderr)
    for n, msg in errs[:20]:
        print('  linha %s: %s' % (n or '-', msg), file=sys.stderr)
    return 2


if __name__ == '__main__':
    sys.exit(main())
