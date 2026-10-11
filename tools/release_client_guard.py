#!/usr/bin/env python3
"""Reject official PokeMMO client content from release staging trees."""
from pathlib import Path

OFFICIAL_PORTABLE_URL = "https://pokemmo.com/en/downloads/portable/"

_CLIENT_FILES = frozenset({
    "pokemmo",
    "pokemmo.exe",
    "pokemmo-debug.exe",
    "pokemmo.jar",
    "pokemmo-debug.jar",
    "pokemmo.sh",
})
_CLIENT_DOWNLOAD_PREFIX = "pokemmo-client"
_CLIENT_ARCHIVE_SUFFIXES = (
    ".zip",
    ".tar",
    ".tar.gz",
    ".tgz",
    ".tar.bz2",
    ".tbz2",
    ".tar.xz",
    ".txz",
    ".7z",
    ".rar",
    ".jar",
    ".dmg",
)


def _is_installed_games_file(parts):
    folded = tuple(part.casefold() for part in parts)
    for index, part in enumerate(folded):
        if part != "prefix":
            continue
        if folded[index + 1:index + 3] == ("drive_c", "games") and index + 3 < len(folded):
            return True
        if (folded[index + 1:index + 4] == ("dosdevices", "c:", "games") and
                index + 4 < len(folded)):
            return True
    return False


def is_official_client_path(path, is_dir):
    """Return whether a staged path crosses the official-client boundary."""
    path = Path(path)
    name = path.name.casefold()
    if is_dir:
        return name.startswith(_CLIENT_DOWNLOAD_PREFIX)
    if name in _CLIENT_FILES:
        return True
    if name.startswith(_CLIENT_DOWNLOAD_PREFIX) and name.endswith(_CLIENT_ARCHIVE_SUFFIXES):
        return True
    return _is_installed_games_file(path.parts)


def find_official_client_content(root):
    """List recognized official-client paths below *root* without changing them."""
    root = Path(root)
    if not root.is_dir():
        return []
    found = []
    for path in sorted(root.rglob("*")):
        if path.is_dir():
            forbidden = is_official_client_path(path.relative_to(root), is_dir=True)
        elif path.is_file():
            forbidden = is_official_client_path(path.relative_to(root), is_dir=False)
        else:
            forbidden = False
        if forbidden:
            found.append(path)
    return found


def reject_official_client_content(root, label):
    """Abort release packaging when *root* contains official client content."""
    root = Path(root)
    found = find_official_client_content(root)
    if not found:
        return
    shown = [path.relative_to(root).as_posix() for path in found[:8]]
    if len(found) > len(shown):
        shown.append(f"... and {len(found) - len(shown)} more")
    paths = ", ".join(shown)
    raise SystemExit(
        f"refusing to create a release archive: {label} contains official PokeMMO client "
        f"content or installed Games content: {paths}. The staged files were left untouched. "
        f"Keep the client outside release inputs; users must independently download and extract "
        f"the portable client from {OFFICIAL_PORTABLE_URL} and supply it with --client."
    )
