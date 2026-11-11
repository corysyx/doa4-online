"""Prints one version's section of CHANGELOG.md (Task 13): the release notes and the release zip's README-netplay.txt
carry it.

  py -3 -I tools/changelog_section.py <x.y.z> [--changelog <CHANGELOG.md>] [--out <file>]

The section is the body under the heading '## <x.y.z>' or '## <x.y.z> (<date>)', up to the next '## ' heading (not
included; '### ' headings inside a section are part of its body), without the heading line and without leading or
trailing blank lines, with LF line ends and one final LF. A CRLF file (a Windows checkout) gives the same body.
--changelog defaults to the repository's CHANGELOG.md; --out writes the body there (UTF-8, no BOM, LF), otherwise it
goes to stdout. Refused (exit 1, the message names the version; no --out file is written): a version that is not
x.y.z, a missing file, no section for the version, an empty section, the version's heading twice.
"""
import argparse
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT = os.path.join(REPO, "CHANGELOG.md")
VERSION = re.compile(r"\d+\.\d+\.\d+")


class NoSection(Exception):
    pass


def section(text, version):
    """The body of `version`'s section of the CHANGELOG text `text` (see the module docstring); NoSection if none."""
    lines = text.replace("\r\n", "\n").split("\n")
    heading = re.compile(r"## " + re.escape(version) + r"( \(.*\))?")
    starts = [i for i, line in enumerate(lines) if heading.fullmatch(line.rstrip())]
    if not starts:
        raise NoSection(f"no section '## {version} (<date>)'")
    if len(starts) > 1:
        raise NoSection(f"the section '## {version}' is there twice")
    body = []
    for line in lines[starts[0] + 1:]:
        if line.startswith("## "):
            break
        body.append(line.rstrip())
    while body and not body[0]:
        body.pop(0)
    while body and not body[-1]:
        body.pop()
    if not body:
        raise NoSection(f"the section '## {version}' is empty")
    return "\n".join(body) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description="Prints one version's section of CHANGELOG.md.")
    ap.add_argument("version", help="x.y.z")
    ap.add_argument("--changelog", default=DEFAULT, help="the CHANGELOG.md (default: the repository's)")
    ap.add_argument("--out", help="write the section to this file (UTF-8, LF) instead of stdout")
    a = ap.parse_args(argv)
    try:
        if not VERSION.fullmatch(a.version):
            raise NoSection(f"the version {a.version!r} is not x.y.z")
        if not os.path.isfile(a.changelog):
            raise NoSection(f"{a.changelog} not found, so there is no section for {a.version}")
        with open(a.changelog, encoding="utf-8-sig") as f:
            body = section(f.read(), a.version)
    except NoSection as e:
        print(f"changelog_section: {a.changelog}: {e}", file=sys.stderr)
        return 1
    if a.out:
        with open(a.out, "w", encoding="utf-8", newline="\n") as f:
            f.write(body)
    else:
        sys.stdout.write(body)
    return 0


if __name__ == "__main__":
    sys.exit(main())
