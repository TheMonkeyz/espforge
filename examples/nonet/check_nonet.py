#!/usr/bin/env python3
"""The no-Wi-Fi build stays without Wi-Fi: fails if forge_net, forge_ota, dns_server, esp_wifi or esp_https_server is
in the example's build (examples/nonet, limited to main's dependency tree by its CMakeLists.txt).

    idf.py -C examples/nonet -B examples/nonet/build build      (or: idf.py -C examples/nonet -B ... reconfigure)
    python examples/nonet/check_nonet.py [build dir or project_description.json]

Reads ESP-IDF's build/project_description.json ("build_components"), and for each component found names the ones
that require it ("build_component_info": reqs, priv_reqs), so the culprit shows. Exit code 0 = none of them.
"""
import json
import os
import sys

FORBIDDEN = ('forge_net', 'forge_ota', 'dns_server', 'esp_wifi', 'esp_https_server')
HERE = os.path.dirname(os.path.abspath(__file__))


def load(arg):
    path = arg or os.path.join(HERE, 'build')
    if os.path.isdir(path):
        path = os.path.join(path, 'project_description.json')
    with open(path, encoding='utf-8') as f:
        return path, json.load(f)


def required_by(desc, name):
    """The build's components that list `name` in their REQUIRES or PRIV_REQUIRES."""
    info = desc.get('build_component_info', {})
    return sorted(c for c, i in info.items() if name in (i.get('reqs') or []) + (i.get('priv_reqs') or []))


def check(desc):
    """The forbidden components in the build, each with who requires it: [(name, [requirers])]"""
    comps = desc.get('build_components') or []
    return [(n, required_by(desc, n)) for n in FORBIDDEN if n in comps]


def main(argv):
    try:
        path, desc = load(argv[1] if len(argv) > 1 else None)
    except (OSError, ValueError) as e:
        print(f'check_nonet: no build description ({e}): build examples/nonet first')
        return 2
    if not desc.get('build_components'):
        print(f'check_nonet: {path} lists no build_components')
        return 2
    bad = check(desc)
    for name, by in bad:
        print(f'FAIL {name} is in the build (required by {", ".join(by) or "?"})')
    if bad:
        return 1
    print(f'ok   none of {", ".join(FORBIDDEN)} in the build ({len(desc["build_components"])} components)')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
