"""package_mod.py - SF-15: builds the redistributable zip of one SenseForge MOD.

Everything comes from mods/<slug>/mod.json, so each MOD declares its own
files and dependencies (UE4SS for an Unreal game, something else - or
nothing - for the next one):

  files[]          repo files (or "build:<name>" from the CMake output dir)
                   copied into the zip at "to"; .ini files can drop
                   developer-only sections (strip_ini_sections).
  dependencies[]   bundle=true  -> downloaded, sha256-verified, extracted
                                   into the zip (minus "exclude");
                   bundle=false -> documented only (render_docs.py).

Usage:
  python tools/mods/package_mod.py expedition-33 [--build-dir DIR]
      [--out dist] [--cache .cache/mod-deps] [--require-ready]
  python tools/mods/package_mod.py --list            # slugs, one per line
  python tools/mods/package_mod.py SLUG --print FIELD  # version, status...

Output: <out>/SenseForge-<slug>-v<version>.zip and a .sha256 next to it,
plus one JSON line on stdout describing the package.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODS = ROOT / "mods"
DEFAULT_BUILD = ROOT / "runtime-asi" / "build" / "Release"
REQUIRED = ("slug", "name", "game", "version", "status", "install_dir",
            "files", "dependencies")
# SF-20: the slug names folders, zips, tags and public URLs.
SLUG_RE = re.compile(r"^[a-z0-9][a-z0-9-]{0,63}$")


class ModError(Exception):
    """A mod.json that cannot be packaged; the message says why."""


def contained(base: Path, relative: str) -> bool:
    """True when `relative` is a plain relative path that stays under base."""
    if not relative or Path(relative).is_absolute() or "\\" in relative             or ":" in relative:
        return False
    target = (base / relative).resolve()
    return target == base.resolve() or base.resolve() in target.parents


def check_paths(mod: dict, path: Path) -> None:
    """SF-20: what a mod.json can read from the repo and write into the zip.

    files[].from: "build:<file name>" or a repo file outside .git;
    files[].to: a path inside the zip; guide_body: a file in mods/<slug>/.
    """
    for entry in mod["files"]:
        source, dest = entry.get("from", ""), entry.get("to", "")
        if source.startswith("build:"):
            name = source[len("build:"):]
            if not name or "/" in name or "\\" in name or name in (".", ".."):
                raise ModError(f"{path}: bad build file '{source}'")
        elif not contained(ROOT, source) or ".git" in Path(source).parts:
            raise ModError(f"{path}: files[].from '{source}' leaves the repo")
        if not contained(Path("stage"), dest) or dest in (".", ""):
            raise ModError(f"{path}: files[].to '{dest}' leaves the package")
    guide = mod.get("guide_body")
    folder = MODS / mod["slug"]
    if guide and not (contained(ROOT, guide)
                      and folder.resolve() in (ROOT / guide).resolve().parents):
        raise ModError(f"{path}: guide_body must be a file in mods/{mod['slug']}/")
    for dep in mod["dependencies"]:
        if not re.match(r"^[a-z0-9][a-z0-9_-]{0,63}$", str(dep.get("id", ""))):
            raise ModError(f"{path}: dependency id {dep.get('id')!r} must be a plain name")
        if dep.get("bundle") and not str(dep.get("url", "")).startswith("https://"):
            raise ModError(f"{path}: bundled dependency '{dep.get('id')}' "
                           "must use an https url")


def load_mod(slug: str) -> dict:
    if not SLUG_RE.match(slug or ""):
        raise ModError(f"invalid slug '{slug}' (use a-z, 0-9 and '-')")
    path = MODS / slug / "mod.json"
    if not path.is_file():
        raise ModError(f"{path} not found")
    mod = json.loads(path.read_text(encoding="utf-8"))
    missing = [key for key in REQUIRED if key not in mod]
    if missing:
        raise ModError(f"{path}: missing {', '.join(missing)}")
    if mod["slug"] != slug:
        raise ModError(f"{path}: slug '{mod['slug']}' != folder '{slug}'")
    if mod["status"] not in ("draft", "ready"):
        raise ModError(f"{path}: status must be draft or ready")
    parts = mod["version"].split(".")
    if len(parts) != 3 or not all(p.isdigit() for p in parts):
        raise ModError(f"{path}: version must be major.minor.patch")
    for dep in mod["dependencies"]:
        if dep.get("bundle") and not (dep.get("url") and dep.get("sha256")):
            raise ModError(f"{path}: bundled dependency '{dep.get('id')}' "
                           "needs url and sha256")
    check_paths(mod, path)
    return mod


def check_ready(mod: dict) -> None:
    """Gate before publishing: only finished MODs are released."""
    problems = []
    if mod["status"] != "ready":
        problems.append("status is not 'ready'")
    guide = mod.get("guide_body")
    if not guide or not (ROOT / guide).is_file():
        problems.append("guide_body missing")
    if not mod.get("known_issues"):
        problems.append("known_issues empty (review incompatibilities)")
    if "performance" not in mod:
        problems.append("performance section missing")
    if problems:
        raise ModError(f"{mod['slug']} is not ready: " + "; ".join(problems))


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fetch(dep: dict, cache: Path) -> Path:
    """Downloads a bundled dependency once per sha256 (own directory)."""
    target_dir = cache / f"{dep['id']}-{dep['sha256'][:16]}"
    target = target_dir / Path(dep["url"]).name
    if target.is_file() and sha256_of(target) == dep["sha256"]:
        return target
    target_dir.mkdir(parents=True, exist_ok=True)
    partial = target.with_suffix(target.suffix + ".part")
    with urllib.request.urlopen(dep["url"], timeout=120) as response, \
            partial.open("wb") as out:
        shutil.copyfileobj(response, out)
    actual = sha256_of(partial)
    if actual != dep["sha256"]:
        partial.unlink()
        raise ModError(f"dependency '{dep['id']}': sha256 mismatch "
                       f"(expected {dep['sha256']}, got {actual})")
    partial.replace(target)
    return target


def extract_dep(archive: Path, dep: dict, stage: Path) -> list[str]:
    exclude = set(dep.get("exclude", []))
    base = stage / dep.get("extract_to", "")
    written = []
    with zipfile.ZipFile(archive) as zf:
        for info in zf.infolist():
            name = info.filename
            if info.is_dir() or name in exclude:
                continue
            dest = (base / name).resolve()
            if stage.resolve() not in dest.parents:
                raise ModError(f"dependency '{dep['id']}': unsafe path {name}")
            dest.parent.mkdir(parents=True, exist_ok=True)
            with zf.open(info) as src, dest.open("wb") as out:
                shutil.copyfileobj(src, out)
            written.append(name)
    return written


def ini_header(line: str) -> str | None:
    """Section name as the runtime parser sees it (ini_config.cpp: a ';' at
    line start or after whitespace starts a comment), or None."""
    stripped = re.split(r"(?:^|(?<=[ 	]));", line, maxsplit=1)[0].strip()
    if stripped.startswith("[") and stripped.endswith("]"):
        return stripped[1:-1].strip()
    return None


def strip_ini(text: str, sections: list[str]) -> str:
    drop = {s.lower() for s in sections}
    out, skipping = [], False
    for line in text.splitlines():
        header = ini_header(line)
        if header is not None:
            skipping = header.lower() in drop
        if not skipping:
            out.append(line)
    while out and not out[-1].strip():
        out.pop()
    return "\r\n".join(out) + "\r\n"


def install_text(mod: dict) -> str:
    manual = [d for d in mod["dependencies"] if not d.get("bundle")]
    lines = [
        f"{mod['name']} - v{mod['version']}",
        "",
        f"1. Extraia tudo na pasta do jogo: {mod['install_dir']}",
        "   (onde fica o " + mod.get("executable", "executavel do jogo") + ").",
    ]
    for i, dep in enumerate(manual, start=2):
        lines.append(f"{i}. Instale antes: {dep['name']} ({dep.get('version', '')})"
                     f" - {dep.get('homepage', '')}")
    lines += [
        "",
        "Guia completo, solucao de problemas e desinstalacao:",
        "https://github.com/Pedro-A-D-S/SenseForge-Mods/blob/main/mods/"
        f"{mod['slug']}.md",
        "",
        "Overlay do ReShade: tecla Home -> aba SenseForge.",
    ]
    return "\r\n".join(lines) + "\r\n"


def package(slug: str, build_dir: Path, out_dir: Path, cache: Path,
            require_ready: bool) -> dict:
    mod = load_mod(slug)
    if require_ready:
        check_ready(mod)
    with tempfile.TemporaryDirectory(prefix=f"sf-{slug}-") as tmp:
        stage = Path(tmp) / "stage"
        stage.mkdir()
        bundled = []
        for dep in mod["dependencies"]:
            if dep.get("bundle"):
                archive = fetch(dep, cache)
                extract_dep(archive, dep, stage)
                bundled.append(f"{dep['id']}@{dep['version']}")
        for entry in mod["files"]:
            source = entry["from"]
            if source.startswith("build:"):
                src = build_dir / source[len("build:"):]
            else:
                src = ROOT / source
            if not src.is_file():
                raise ModError(f"file not found: {src}")
            dest = stage / entry["to"]
            dest.parent.mkdir(parents=True, exist_ok=True)
            if entry.get("strip_ini_sections"):
                text = src.read_text(encoding="utf-8")
                dest.write_text(strip_ini(text, entry["strip_ini_sections"]),
                                encoding="utf-8", newline="")
            else:
                shutil.copyfile(src, dest)
        (stage / f"SenseForge-{slug}-INSTALL.txt").write_text(
            install_text(mod), encoding="utf-8", newline="")

        out_dir.mkdir(parents=True, exist_ok=True)
        zip_path = out_dir / f"SenseForge-{slug}-v{mod['version']}.zip"
        with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
            for path in sorted(stage.rglob("*")):
                if path.is_file():
                    zf.write(path, path.relative_to(stage).as_posix())
    digest = sha256_of(zip_path)
    zip_path.with_suffix(".zip.sha256").write_text(
        f"{digest}  {zip_path.name}\n", encoding="utf-8")
    return {"slug": slug, "version": mod["version"], "status": mod["status"],
            "zip": str(zip_path), "sha256": digest, "bundled": bundled}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("slug", nargs="?")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--print", dest="field")
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--out", type=Path, default=ROOT / "dist")
    parser.add_argument("--cache", type=Path,
                        default=ROOT / ".cache" / "mod-deps")
    parser.add_argument("--require-ready", action="store_true")
    args = parser.parse_args()
    try:
        if args.list:
            for path in sorted(MODS.glob("*/mod.json")):
                print(path.parent.name)
            return 0
        if not args.slug:
            parser.error("slug required")
        if args.field:
            print(load_mod(args.slug)[args.field])
            return 0
        result = package(args.slug, args.build_dir, args.out, args.cache,
                         args.require_ready)
        print(json.dumps(result))
        return 0
    except ModError as error:
        print(f"package_mod: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
