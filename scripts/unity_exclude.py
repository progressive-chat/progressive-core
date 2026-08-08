#!/usr/bin/env python3
# scripts/unity_exclude.py — progressive-core unity-build helper.
#
# The vendored native codebase defines many helper functions at namespace
# scope in MULTIPLE .cpp files (legacy copies of extractJson*, base64*,
# is*Message, …) and in headers (out-of-line, non-inline definitions).
# Separate TUs tolerate this (the linker keeps one copy); a unity build
# merges them into one TU and fails with "redefinition".
#
# This script prints the .cpp files that MUST stay standalone TUs:
#   - files defining a function whose name is ALSO defined by another .cpp,
#   - files defining a function whose name is defined out-of-line in a
#     native/include header,
#   - draft_manager_full.cpp (namespace-scope enum in its header).
#
# Heuristic on purpose: over-exclusion is harmless (standalone TUs compile
# at normal speed), under-exclusion breaks the unity build.
#
# Run: unity_exclude.py  -> one filename per line on stdout

import os
import re
import sys
import glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(ROOT, "native", "src")
HDR_DIR = os.path.join(ROOT, "native", "include", "progressive")

# Lines that look like function definitions in a .cpp (static or namespace
# scope). Deliberately loose: `bool isTextMessage(const ...)` etc.
DEF_RE = re.compile(
    r"^(?:static\s+|inline\s+)*[\w:<> ,\*&]+\s+([A-Za-z_][A-Za-z0-9_]*)\s*\([^;]*\)\s*\{"
)
SKIP_PREFIXES = ("if(", "for(", "while(", "switch(", "return", "//", "/*", "*",
                 "#include", "using ", "typedef", "enum ", "struct ", "class ",
                 "namespace ")


TYPE_DEF_RE = re.compile(
    r"^(?:enum\s+class\s+|enum\s+|using\s+)([A-Za-z_][A-Za-z0-9_]*)\s*(?::|=|\{|;)"
)


def cpp_defined_names(path):
    names = set()
    try:
        with open(path, encoding="utf-8", errors="ignore") as f:
            for line in f:
                ln = line.strip()
                if not ln or ln.startswith(SKIP_PREFIXES):
                    continue
                m = DEF_RE.match(ln)
                if m:
                    names.add(m.group(1))
                    continue
                m = TYPE_DEF_RE.match(ln)
                if m:
                    names.add(m.group(1))
    except OSError:
        pass
    return names


def header_defined_names(path):
    """Out-of-line (non-inline) function definitions inside a header."""
    names = set()
    try:
        text = open(path, encoding="utf-8", errors="ignore").read()
    except OSError:
        return names
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r'"(?:[^"\\]|\\.)*"', '""', text)
    lines = [l for l in text.splitlines() if not l.strip().startswith("#")]
    joined = "\n".join(lines)
    for m in re.finditer(
        r"(?m)^(?!\s*(?:using|typedef|enum|struct|class|namespace)\b)"
        r"[\w:<> ,\*&]+\s+([A-Za-z_][A-Za-z0-9_]*)\s*\([^;]*\)\s*\{",
        joined,
    ):
        names.add(m.group(1))
    for m in re.finditer(
        r"(?m)^(?:enum\s+class\s+|enum\s+|using\s+|struct\s+|class\s+)"
        r"([A-Za-z_][A-Za-z0-9_]*)\s*(?::|=|\{|;)",
        joined,
    ):
        names.add(m.group(1))
    return names


# Known same-name STATIC helper clusters across the vendored codebase
# (discovered by the compiler during unity iterations). Files defining any
# of these cannot be merged with each other.
KNOWN_CLUSTERS = {
    "extractStr", "extractJsonString", "extractJsonBool", "extractJsonObject",
    "extractJsonInt64", "base64UrlToBase64", "base64ToBase64Url",
    "base64ToUnpaddedBase64", "isEmail", "randomString", "createAccount",
    "signMessage", "getIdentityKey", "base64Encode", "base64Decode",
    "extractUsefulTextFromReply", "formatSpoilerTextFromHtml",
    "toLower", "buildUserPermalink", "isPermalink", "formatFingerprint",
    "verifySignature", "getFingerprint", "parseRoomNameContent",
    "extractInt", "extractInt64", "extractBool", "extractString",
    "escapeHtml", "stripHtml", "plainTextFromHtml", "htmlToPlainText",
    "isToday", "isYesterday", "formatTime", "formatLastMessagePreview",
    "ensureTrailingSlash", "stripTags", "extractLinks",
}


def main():
    cpps = sorted(glob.glob(os.path.join(SRC_DIR, "*.cpp")))
    hdrs = sorted(glob.glob(os.path.join(HDR_DIR, "*.hpp")))

    header_names = set()
    for path in hdrs:
        header_names |= header_defined_names(path)

    bad = set()
    # Headers whose namespace-scope definitions can never merge with other
    # files (draft_manager_full.hpp: enum; room_sort.hpp: enum RoomTag that
    # clashes with room_content.hpp's namespace RoomTag).
    for h in ("draft_manager_full.hpp", "room_sort.hpp"):
        for path in cpps:
            try:
                text = open(path, encoding="utf-8", errors="ignore").read()
            except OSError:
                continue
            if h in text:
                bad.add(os.path.basename(path))

    # A TYPE NAME defined in TWO DIFFERENT headers (struct/enum/using with
    # the same name) can never be merged into one TU — exclude cpps that
    # include either of the clashing headers.
    header_type_names = {}
    for path in hdrs:
        for name in header_defined_names(path):
            header_type_names.setdefault(name, set()).add(os.path.basename(path))
    clashing_headers = set()
    for name, hset in header_type_names.items():
        if len(hset) > 1:
            clashing_headers |= hset
    for path in cpps:
        try:
            text = open(path, encoding="utf-8", errors="ignore").read()
        except OSError:
            continue
        if any(h in text for h in clashing_headers):
            bad.add(os.path.basename(path))

    # Files defining any KNOWN same-name helper cluster or any out-of-line
    # header-defined function can never merge with their sibling definers.
    for path in cpps:
        names = cpp_defined_names(path)
        if names & KNOWN_CLUSTERS or names & header_names:
            bad.add(os.path.basename(path))

    for f in sorted(bad):
        print(f)


if __name__ == "__main__":
    main()
