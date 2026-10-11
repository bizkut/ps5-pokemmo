#!/usr/bin/env python3
"""Consumer tests for keeping the official PokeMMO client out of releases."""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

SOURCE_ROOT = Path(__file__).resolve().parent.parent
MARKER = b"harmless fixture marker\n"


def write_file(path, content=MARKER):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)
    return path


def run_cli(*arguments, cwd, env=None):
    command_env = os.environ.copy()
    command_env["PYTHONDONTWRITEBYTECODE"] = "1"
    if env:
        command_env.update(env)
    return subprocess.run(
        [sys.executable, *map(str, arguments)],
        cwd=cwd,
        env=command_env,
        capture_output=True,
        text=True,
        timeout=30,
        check=False,
    )


class PackagingFixture(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name) / "project"

    def tearDown(self):
        self.temporary_directory.cleanup()

    def prepare_package_release(self):
        script = self.root / "app/ps5/tools/package-release.py"
        script.parent.mkdir(parents=True)
        shutil.copy2(SOURCE_ROOT / "app/ps5/tools/package-release.py", script)
        helper = self.root / "tools/release_client_guard.py"
        helper.parent.mkdir(parents=True)
        shutil.copy2(SOURCE_ROOT / "tools/release_client_guard.py", helper)

        param = {
            "titleId": "PPSA99221",
            "localizedParameters": {"en-US": {"titleName": "PokeMMO PS5"}},
        }
        param_path = self.root / "app/ps5/sce_sys/param.json"
        param_path.parent.mkdir(parents=True)
        param_path.write_text(json.dumps(param), encoding="utf-8")

        title = self.root / "app/dist/PPSA99221"
        write_file(title / "eboot.bin")
        components = title / "licenses/components.json"
        components.parent.mkdir(parents=True)
        components.write_text("[]", encoding="utf-8")
        return script, title

    def run_package_release(self, script):
        return run_cli(script, "test", "--date", "2026-01-02", cwd=self.root)

    def prepare_make_bundle(self):
        script = self.root / "tools/make-bundle.py"
        script.parent.mkdir(parents=True)
        shutil.copy2(SOURCE_ROOT / "tools/make-bundle.py", script)
        shutil.copy2(SOURCE_ROOT / "tools/release_client_guard.py", script.parent / "release_client_guard.py")
        write_file(script.parent / "install.py", b"# fixture installer\n")

        console_module = self.root / "vendor/PS5_Vulkan/tools/ps5_console.py"
        write_file(
            console_module,
            b"def load_settings():\n    raise AssertionError('no-fetch contacted the console')\n",
        )
        title = self.root / "app/dist/PPSA99221"
        write_file(title / "eboot.bin")
        write_file(title / "sce_sys/param.sfo")

        mesa = self.root / "mesa"
        write_file(mesa / "bin/opengl32.dll")
        write_file(mesa / "bin/libgallium_wgl.dll")

        bundle = self.root / "out/PokeMMO-PS5"
        write_file(bundle / "runtime/pokemmo/prefix/user.reg", b"WINE REGISTRY Version 2\n")
        return script, title, mesa, bundle

    def run_make_bundle(self, script, mesa):
        return run_cli(
            script,
            "--out",
            self.root / "out",
            "--no-fetch",
            cwd=self.root,
            env={"MESA_INSTALL": str(mesa)},
        )

    def assert_client_rejection(self, result, expected_path):
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(expected_path, result.stderr)


class PackageReleaseBoundaryTests(PackagingFixture):
    def test_clean_title_with_project_names_and_legitimate_pe_files_is_packaged(self):
        script, title = self.prepare_package_release()
        allowed = {
            "runtime/pokemmo/wine/bin/wine64": b"wine runtime\n",
            "runtime/pokemmo/prefix/drive_c/windows/system32/kernel32.dll": b"legitimate PE DLL\n",
            "PokeMMO-PS5-launcher.exe": b"project executable\n",
            "tools/PokeMMO.exe.manifest": b"manifest, not the client binary\n",
        }
        for relative, content in allowed.items():
            write_file(title / relative, content)

        result = self.run_package_release(script)

        self.assertEqual(result.returncode, 0, result.stderr)
        archive = self.root / "app/dist/release-test/app-test.zip"
        self.assertTrue(archive.is_file())
        with zipfile.ZipFile(archive) as release_zip:
            for relative, content in allowed.items():
                self.assertEqual(release_zip.read(f"PPSA99221/{relative}"), content)

    def test_recognized_client_file_and_download_variants_are_rejected_unchanged(self):
        cases = (
            ("nested/windows/PoKeMmO.ExE", False),
            ("nested/windows/POKEMMO-debug.EXE", False),
            ("native/linux/bin/PokeMMO", False),
            ("legacy/PokeMMO.JAR", False),
            ("legacy/POKEMMO-debug.Jar", False),
            ("portable/pokemmo.SH", False),
            ("downloads/PoKeMmO-ClIeNt-Windows.ZIP", False),
            ("downloads/PokeMMO-Client-linux.tar.gz", False),
            ("downloads/POKEMMO-client-macos", True),
        )
        for relative, is_directory in cases:
            with self.subTest(relative=relative):
                with tempfile.TemporaryDirectory() as temporary_directory:
                    previous_root = self.root
                    self.root = Path(temporary_directory) / "project"
                    try:
                        script, title = self.prepare_package_release()
                        private_path = title / relative
                        marker = private_path / "data/main.properties" if is_directory else private_path
                        write_file(marker)

                        result = self.run_package_release(script)

                        self.assert_client_rejection(result, relative)
                        self.assertFalse((self.root / "app/dist/release-test").exists())
                        self.assertEqual(marker.read_bytes(), MARKER)
                    finally:
                        self.root = previous_root

    def test_installed_games_assets_are_rejected_unchanged(self):
        script, title = self.prepare_package_release()
        private = write_file(
            title / "runtime/pokemmo/prefix/drive_c/Games/PokeMMO/data/main.properties"
        )

        result = self.run_package_release(script)

        self.assert_client_rejection(
            result,
            "runtime/pokemmo/prefix/drive_c/Games/PokeMMO/data/main.properties",
        )
        self.assertFalse((self.root / "app/dist/release-test").exists())
        self.assertEqual(private.read_bytes(), MARKER)


class MakeBundleBoundaryTests(PackagingFixture):
    def test_clean_no_fetch_runtime_is_packaged(self):
        script, title, mesa, bundle = self.prepare_make_bundle()
        allowed = {
            "runtime/pokemmo/wine/bin/wine64": b"wine runtime\n",
            "runtime/pokemmo/prefix/drive_c/windows/system32/kernel32.dll": b"legitimate PE DLL\n",
            "runtime/pokemmo/probes/render-test.exe": b"project probe\n",
        }
        for relative, content in allowed.items():
            write_file(bundle / relative, content)
        write_file(title / "PokeMMO-PS5-helper.exe", b"title helper\n")

        result = self.run_make_bundle(script, mesa)

        self.assertEqual(result.returncode, 0, result.stderr)
        archive = self.root / "out/PokeMMO-PS5.zip"
        self.assertTrue(archive.is_file())
        with zipfile.ZipFile(archive) as bundle_zip:
            for relative, content in allowed.items():
                self.assertEqual(bundle_zip.read(f"PokeMMO-PS5/{relative}"), content)
            self.assertIn("PokeMMO-PS5/README.txt", bundle_zip.namelist())

    def test_title_client_binary_is_rejected_before_archive_creation(self):
        script, title, mesa, _bundle = self.prepare_make_bundle()
        private = write_file(title / "nested/PokeMMO-Debug.exe")

        result = self.run_make_bundle(script, mesa)

        self.assert_client_rejection(result, "nested/PokeMMO-Debug.exe")
        self.assertFalse((self.root / "out/PokeMMO-PS5.zip").exists())
        self.assertEqual(private.read_bytes(), MARKER)

    def test_stale_download_archive_is_rejected_unchanged_in_no_fetch_bundle(self):
        script, _title, mesa, bundle = self.prepare_make_bundle()
        private = write_file(bundle / "runtime/pokemmo/downloads/pokemmo-client-portable.ZIP")

        result = self.run_make_bundle(script, mesa)

        self.assert_client_rejection(
            result,
            "runtime/pokemmo/downloads/pokemmo-client-portable.ZIP",
        )
        self.assertFalse((self.root / "out/PokeMMO-PS5.zip").exists())
        self.assertEqual(private.read_bytes(), MARKER)

    def test_stale_games_layouts_are_rejected_unchanged_in_no_fetch_bundle(self):
        cases = (
            "runtime/pokemmo/prefix/drive_c/Games/PokeMMO/data/strings_en.xml",
            "runtime/pokemmo/prefix/dosdevices/c:/Games/PokeMMO/data/strings_en.xml",
        )
        for relative in cases:
            with self.subTest(relative=relative):
                with tempfile.TemporaryDirectory() as temporary_directory:
                    previous_root = self.root
                    self.root = Path(temporary_directory) / "project"
                    try:
                        script, _title, mesa, bundle = self.prepare_make_bundle()
                        private = write_file(bundle / relative)

                        result = self.run_make_bundle(script, mesa)

                        self.assert_client_rejection(result, relative)
                        self.assertFalse((self.root / "out/PokeMMO-PS5.zip").exists())
                        self.assertEqual(private.read_bytes(), MARKER)
                    finally:
                        self.root = previous_root


if __name__ == "__main__":
    unittest.main()
