#!/usr/bin/env python3
"""test_package_mod.py - SF-20 regression tests for the mod.json checks.

A mod.json decides what packaging reads from the repo, what lands in the
player zip and which page is published. These tests pin the containment
rules (files[].from/to, guide_body, slug, https dependencies) and the
comment-aware [Dev] stripping. No network, no build output needed.

Run: python -I tools/mods/test_package_mod.py
"""

import copy
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import package_mod  # noqa: E402
from package_mod import ModError, check_paths, load_mod, strip_ini  # noqa: E402

REAL = load_mod("expedition-33")
WHERE = Path("mods/expedition-33/mod.json")


def with_file(**entry):
    mod = copy.deepcopy(REAL)
    mod["files"] = [entry]
    return mod


class PathContainment(unittest.TestCase):
    def test_real_mod_passes(self):
        check_paths(REAL, WHERE)

    def test_from_outside_repo_rejected(self):
        for source in ("../secret.txt", "C:/Windows/win.ini", "/etc/passwd",
                       ".git/config", "public/.git/config", "a\\..\\..\\b"):
            with self.subTest(source=source), self.assertRaises(ModError):
                check_paths(with_file(**{"from": source, "to": "x.txt"}), WHERE)

    def test_build_name_must_be_plain(self):
        for source in ("build:", "build:../x.dll", "build:sub/x.dll"):
            with self.subTest(source=source), self.assertRaises(ModError):
                check_paths(with_file(**{"from": source, "to": "x.dll"}), WHERE)

    def test_to_outside_package_rejected(self):
        for dest in ("../x.dll", "C:/x.dll", "/x.dll", "", "a/../../x.dll"):
            with self.subTest(dest=dest), self.assertRaises(ModError):
                check_paths(with_file(**{"from": "README.md", "to": dest}), WHERE)

    def test_guide_body_must_stay_in_mod_folder(self):
        for guide in ("README.md", "public/.git/config", ".git/config",
                      "mods/other/guide.md", "mods/expedition-33/../../README.md"):
            mod = copy.deepcopy(REAL)
            mod["guide_body"] = guide
            with self.subTest(guide=guide), self.assertRaises(ModError):
                check_paths(mod, WHERE)

    def test_bundled_dependency_needs_https(self):
        mod = copy.deepcopy(REAL)
        mod["dependencies"][0]["url"] = "http://example.com/ue4ss.zip"
        with self.assertRaises(ModError):
            check_paths(mod, WHERE)


class Slug(unittest.TestCase):
    def test_bad_slugs_rejected(self):
        for slug in ("../mods", "Expedition", "a b", "a;rm", "-x", ""):
            with self.subTest(slug=slug), self.assertRaises(ModError):
                load_mod(slug)


class DependencyId(unittest.TestCase):
    def test_id_names_a_cache_folder(self):
        for dep_id in ("../x", "a/b", "", "Ue4SS"):
            with self.subTest(dep_id=dep_id):
                mod = copy.deepcopy(REAL)
                mod["dependencies"] = [{"id": dep_id, "bundle": False}]
                with self.assertRaises(ModError):
                    check_paths(mod, WHERE)


class StripIni(unittest.TestCase):
    def test_header_with_comment_is_stripped(self):
        text = ("[SenseForge]\nenabled=1\n[Dev] ; tools\nagent_autostart=1\n"
                "[Events]\nfile=a.ndjson\n")
        out = strip_ini(text, ["Dev"])
        self.assertNotIn("agent_autostart", out)
        self.assertIn("[Events]", out)

    def test_semicolon_inside_name_is_not_a_comment(self):
        self.assertEqual(package_mod.ini_header("[a;b]"), "a;b")


if __name__ == "__main__":
    unittest.main(verbosity=2)
