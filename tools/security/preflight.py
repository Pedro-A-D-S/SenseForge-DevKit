"""preflight.py - SF-20: security gate that runs before any MOD is published.

Runs on every PR (ci.yml) and again inside the publish job, before the
public-repo token is used (_publish-mod.yml). Every check reads repository
files only: nothing is built, executed or downloaded.

  workflows   every `uses:` is pinned to a full commit SHA; no `${{ }}`
              expression from inputs/event data is pasted into a `run:` script;
              the job holding MODS_REPO_TOKEN runs in an environment and never
              persists a checkout credential
  third-party every pin in tools/fetch_third_party.sh is a full commit SHA
  mods        every mods/<slug>/mod.json passes package_mod's checks (slug,
              contained paths, https dependencies)
  package     the shipped senseforge_user.ini has no enabled [Dev] switch;
              the shipped UE4SS mods.txt keeps the console and cheat mods off;
              manifest ids are plain names and ui.guide_url is https
  secrets     no token, key or private-key block in any tracked text file

Usage: python -I tools/security/preflight.py   (exit 1 lists every problem)
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "mods"))
from package_mod import ModError, ini_header, load_mod  # noqa: E402

SHA_RE = re.compile(r"^[0-9a-f]{40}$")
ID_RE = re.compile(r"^[A-Za-z0-9_.-]{1,64}$")
USES_RE = re.compile(r"^\s*-?\s*uses:\s*([^\s#]+)", re.M)
# Expressions an outside actor can influence; they must reach scripts via env.
UNSAFE_EXPR_RE = re.compile(r"\$\{\{\s*(inputs\.|github\.event\.|github\.head_ref"
                            r"|steps\.[^}]*outputs|needs\.[^}]*outputs)")
SECRET_RES = [
    re.compile(r"\bgh[pousr]_[A-Za-z0-9]{36,}\b"),
    re.compile(r"\bgithub_pat_[A-Za-z0-9_]{60,}\b"),
    re.compile(r"\bAKIA[0-9A-Z]{16}\b"),
    re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH |DSA |PGP )?PRIVATE KEY"),
    re.compile(r"\blin_(?:api|oauth)_[A-Za-z0-9]{32,}\b"),
    re.compile(r"\bxox[abprs]-[A-Za-z0-9-]{10,}\b"),
    re.compile(r"\bsk-ant-[A-Za-z0-9_-]{20,}\b"),
]
DEV_SWITCHES = ("agent_autostart", "creator_tools")
UE4SS_OFF = ("CheatManagerEnablerMod", "ConsoleCommandsMod", "ConsoleEnablerMod")
TOKEN_SECRET = "MODS_REPO_TOKEN"


def run_blocks(text: str) -> list[str]:
    """Bodies of `run:` steps (inline or block scalar) of a workflow."""
    blocks, lines = [], text.splitlines()
    for i, line in enumerate(lines):
        m = re.match(r"^(\s*)(?:-\s+)?run:\s*(.*)$", line)
        if not m:
            continue
        rest = m.group(2)
        if rest and rest[0] not in "|>":
            blocks.append(rest)
            continue
        indent = len(m.group(1))
        body = []
        for nxt in lines[i + 1:]:
            if nxt.strip() and len(nxt) - len(nxt.lstrip()) <= indent:
                break
            body.append(nxt)
        blocks.append("\n".join(body))
    return blocks


def check_workflows(problems: list[str]) -> None:
    for wf in sorted((ROOT / ".github" / "workflows").glob("*.y*ml")):
        text = wf.read_text(encoding="utf-8")
        rel = wf.relative_to(ROOT).as_posix()
        for ref in USES_RE.findall(text):
            if ref.startswith("./"):
                continue
            if "@" not in ref or not SHA_RE.match(ref.rsplit("@", 1)[1]):
                problems.append(f"{rel}: action '{ref}' is not pinned to a commit SHA")
        for body in run_blocks(text):
            for expr in UNSAFE_EXPR_RE.findall(body):
                problems.append(f"{rel}: run script interpolates '${{{{ {expr}' "
                                "(pass it through env: instead)")
        if f"secrets.{TOKEN_SECRET}" in text and "workflow_call" in text:
            if not re.search(r"^\s+environment:", text, re.M):
                problems.append(f"{rel}: job using {TOKEN_SECRET} has no environment:")
            for block in re.split(r"\n\s*- (?=uses:|name:)", text):
                if "actions/checkout@" in block and "persist-credentials: false" not in block:
                    problems.append(f"{rel}: checkout without persist-credentials: false")


def check_third_party(problems: list[str]) -> None:
    script = (ROOT / "tools" / "fetch_third_party.sh").read_text(encoding="utf-8")
    for name, ref in re.findall(r'^\s*"([^"|]+)\|[^"|]+\|([^"]+)"', script, re.M):
        if not SHA_RE.match(ref):
            problems.append(f"fetch_third_party.sh: {name} pinned to '{ref}', not a commit SHA")


def check_ini(text: str, where: str, problems: list[str]) -> None:
    section = ""
    for line in text.splitlines():
        header = ini_header(line)
        if header is not None:
            section = header.lower()
            continue
        if section == "dev" and "=" in line:
            key, value = (p.strip() for p in line.split(";")[0].split("=", 1))
            if key in DEV_SWITCHES and value not in ("0", "false", "no", ""):
                problems.append(f"{where}: [Dev] {key}={value} would ship enabled")


def stripped_ini(entry: dict) -> str:
    from package_mod import strip_ini  # noqa: PLC0415
    text = (ROOT / entry["from"]).read_text(encoding="utf-8")
    sections = entry.get("strip_ini_sections")
    return strip_ini(text, sections) if sections else text


def check_manifest(path: Path, where: str, problems: list[str]) -> None:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    ids = [s.get("id", "") for s in manifest.get("states", [])]
    ids += [s.get("category", "") for s in manifest.get("states", []) if "category" in s]
    ids += [e.get("id", "") for e in manifest.get("event_effects", [])]
    for value in ids:
        if not ID_RE.match(str(value)):
            problems.append(f"{where}: id {value!r} must match {ID_RE.pattern}")
    ui = manifest.get("ui") or {}
    url = ui.get("guide_url")
    if url and not str(url).startswith("https://"):
        problems.append(f"{where}: ui.guide_url must be an https:// link")


def check_mods(problems: list[str]) -> None:
    for folder in sorted((ROOT / "mods").glob("*/mod.json")):
        slug = folder.parent.name
        try:
            mod = load_mod(slug)
        except ModError as error:
            problems.append(f"mods/{slug}: {error}")
            continue
        for entry in mod["files"]:
            source, dest = entry["from"], entry["to"]
            if source.startswith("build:"):
                continue
            where = f"mods/{slug} -> {dest}"
            if dest.lower().endswith(".ini") and Path(dest).name == "senseforge_user.ini":
                check_ini(stripped_ini(entry), where, problems)
            if Path(dest).name.lower() == "mods.txt":
                text = (ROOT / source).read_text(encoding="utf-8")
                for name in UE4SS_OFF:
                    if re.search(rf"^\s*{name}\s*:\s*1\b", text, re.M):
                        problems.append(f"{where}: {name} is enabled")
            if source.endswith(".sense.json"):
                check_manifest(ROOT / source, where, problems)


def check_secrets(problems: list[str]) -> None:
    files = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z"], check=True,
                           capture_output=True).stdout.decode("utf-8").split("\0")
    for rel in filter(None, files):
        path = ROOT / rel
        try:
            data = path.read_bytes()
        except OSError:
            continue
        if b"\0" in data[:8192] or len(data) > 5_000_000:
            continue  # binary or huge generated file
        text = data.decode("utf-8", errors="replace")
        for pattern in SECRET_RES:
            match = pattern.search(text)
            if match:
                line = text.count("\n", 0, match.start()) + 1
                problems.append(f"{rel}:{line}: looks like a secret ({pattern.pattern[:24]}...)")


def main() -> int:
    problems: list[str] = []
    for check in (check_workflows, check_third_party, check_mods, check_secrets):
        check(problems)
    if problems:
        print("preflight: FAILED", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1
    print("preflight: OK (workflows, third-party pins, mods, package, secrets)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
