"""render_docs.py - SF-15: catalog + per-MOD guides of the public repo.

Reads every mods/<slug>/mod.json (plus its hand-written guide_body) and
writes, into a checkout of Pedro-A-D-S/SenseForge-Mods:

  README.md          "MODs para download" catalog (ready MODs only)
  mods/<slug>.md     user guide: features, requirements, dependencies with
                     install steps, performance, known incompatibilities,
                     then the hand-written body (install, first use,
                     troubleshooting, uninstall)

Generated from the same mod.json the packager uses, so the published
requirements can never drift from what is inside the zip.

Usage:
  python tools/mods/render_docs.py --out <public-repo-checkout>
      [--release-date YYYY-MM-DD --released SLUG]   # stamps one release
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODS = ROOT / "mods"
PUBLIC = "https://github.com/Pedro-A-D-S/SenseForge-Mods"


def release_tag(mod: dict) -> str:
    return f"{mod['slug']}-v{mod['version']}"


def download_url(mod: dict) -> str:
    return (f"{PUBLIC}/releases/download/{release_tag(mod)}/"
            f"SenseForge-{mod['slug']}-v{mod['version']}.zip")


def load_all() -> list[dict]:
    mods = []
    for path in sorted(MODS.glob("*/mod.json")):
        mods.append(json.loads(path.read_text(encoding="utf-8")))
    return mods


def render_catalog(mods: list[dict], dates: dict) -> str:
    ready = [m for m in mods if m.get("status") == "ready"]
    lines = [
        '<p align="center"><img src="assets/brand/banner.png" '
        'alt="SenseForge" width="100%"></p>',
        "",
        "# SenseForge MODs",
        "",
        "Suporte nativo ao **DualSense (PS5)** em jogos de PC: gatilhos "
        "adaptativos, lightbar, vibração HD e alto-falante do controle "
        "reagindo ao que acontece no jogo, em tempo real.",
        "",
        "## MODs para download",
        "",
        "| Jogo | Versão | Lançado em | Download | Guia |",
        "| :--- | :---: | :---: | :---: | :---: |",
    ]
    for mod in ready:
        date = dates.get(mod["slug"])
        # Never link a zip before its Release exists (first publish of the
        # catalog happens before the first tag).
        download = (f"[zip]({download_url(mod)})" if date
                    else "*em breve*")
        lines.append(
            f"| **{mod['game']}** | v{mod['version']} | {date or '-'} | "
            f"{download} | [como instalar](mods/{mod['slug']}.md) |")
    if not ready:
        lines.append("| *(em breve)* | | | | |")
    lines += [
        "",
        "Todas as versões ficam em [Releases]"
        f"({PUBLIC}/releases). Cada zip vem com um arquivo `.sha256` para "
        "conferir o download.",
        "",
        "## Como funciona",
        "",
        "Cada MOD é um add-on do [ReShade](https://reshade.me) "
        "(`senseforge.<jogo>.addon64`) com um perfil do jogo. Os ajustes "
        "ficam na aba **SenseForge** do overlay do ReShade (tecla **Home**): "
        "presets, perfis Fraco/Médio/Forte e ajuste de cada efeito. As "
        "dependências mudam de jogo para jogo e estão listadas no guia de "
        "cada MOD.",
        "",
        "## Problemas e sugestões",
        "",
        f"Abra uma [issue]({PUBLIC}/issues) informando o jogo, a versão do "
        "MOD e o log mais recente de `%LOCALAPPDATA%\\senseforge\\logs\\`.",
        "",
        "---",
        "<sub>SenseForge não tem relação com a Sony Interactive "
        "Entertainment nem com os estúdios dos jogos. DualSense e "
        "PlayStation são marcas da Sony Interactive Entertainment.</sub>",
        "",
    ]
    return "\n".join(lines)


def render_guide(mod: dict, date: str) -> str:
    lines = [
        f'<p align="center"><img src="../assets/brand/banner.png" '
        f'alt="SenseForge" width="100%"></p>',
        "",
        f"# {mod['name']}",
        "",
        f"**Versão {mod['version']}**"
        + (f" · lançada em {date} · [baixar o zip]({download_url(mod)})"
           if date else " · download em breve"),
        "",
        mod.get("summary", ""),
        "",
        "## O que o MOD faz",
        "",
    ]
    lines += [f"- {feature}" for feature in mod.get("features", [])]
    lines += ["", "## Requisitos", ""]
    lines += [f"- {req}" for req in mod.get("requirements", [])]
    lines += ["", "## Dependências", ""]
    for dep in mod["dependencies"]:
        how = ("já vem no zip" if dep.get("bundle")
               else "**instale antes do MOD**")
        lines.append(f"### {dep['name']} ({dep.get('version', '')}): {how}")
        lines.append("")
        if dep.get("why"):
            lines.append(dep["why"])
            lines.append("")
        if dep.get("homepage"):
            lines.append(f"Site: <{dep['homepage']}> · Licença: "
                         f"{dep.get('license', '-')}")
            lines.append("")
        for i, step in enumerate(dep.get("install_steps", []), start=1):
            lines.append(f"{i}. {step}")
        if dep.get("install_steps"):
            lines.append("")
    perf = mod.get("performance", {})
    lines += ["## Desempenho e stutter", ""]
    if perf.get("summary"):
        lines += [perf["summary"], ""]
    for row in perf.get("results", []):
        lines.append(f"- {row}")
    for note in perf.get("notes", []):
        lines.append(f"- {note}")
    lines += ["", "## Incompatibilidades conhecidas", ""]
    for issue in mod.get("known_issues", []):
        lines.append(f"- **{issue['title']}**: {issue['detail']}")
    lines.append("")
    body = mod.get("guide_body")
    if body and (ROOT / body).is_file():
        lines.append((ROOT / body).read_text(encoding="utf-8").rstrip())
        lines.append("")
    lines += [
        "## English (short)",
        "",
        f"{mod['game']} DualSense MOD (adaptive triggers, lightbar, HD "
        "haptics). Install ReShade *with full add-on support*, extract the "
        f"zip into `{mod['install_dir']}`, press **Home** and open the "
        "**SenseForge** tab.",
        "",
        "---",
        f"<sub>{mod.get('credits', '')}</sub>",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--released", help="slug released now")
    parser.add_argument("--release-date",
                        default=dt.date.today().isoformat())
    args = parser.parse_args()

    mods = load_all()
    dates_file = args.out / "mods" / "release-dates.json"
    dates = {}
    if dates_file.is_file():
        dates = json.loads(dates_file.read_text(encoding="utf-8"))
    if args.released:
        dates[args.released] = args.release_date

    (args.out / "mods").mkdir(parents=True, exist_ok=True)
    (args.out / "README.md").write_text(render_catalog(mods, dates),
                                        encoding="utf-8")
    for mod in mods:
        if mod.get("status") == "ready":
            (args.out / "mods" / f"{mod['slug']}.md").write_text(
                render_guide(mod, dates.get(mod["slug"], "")),
                encoding="utf-8")
    dates_file.write_text(json.dumps(dates, indent=2) + "\n",
                          encoding="utf-8")
    print(f"render_docs: {len(mods)} MOD(s) -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
