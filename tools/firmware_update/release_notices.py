"""Public licence and notice files shipped with every firmware archive."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def release_notice_files() -> list[tuple[Path, str]]:
    """Return checked-in public files and their archive names.

    Only the dedicated licences directory is enumerated; files near a signed
    release, uploader, or offline key are never pulled into an archive.
    """
    names = ("LICENSE", "NOTICE", "THIRD-PARTY-NOTICES.md")
    entries = [(ROOT / name, name) for name in names]
    licences = sorted((ROOT / "licenses").glob("*.txt"))
    if not licences:
        raise FileNotFoundError("no third-party licence texts found in licenses/")
    entries.extend((path, f"licenses/{path.name}") for path in licences)
    for path, _ in entries:
        if not path.is_file():
            raise FileNotFoundError(f"release notice is missing: {path}")
    return entries
