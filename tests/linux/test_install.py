import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[2] / "linux" / "install.py"
spec = importlib.util.spec_from_file_location("install", MODULE)
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class InstallTests(unittest.TestCase):
    def test_install_is_repeatable_and_uninstall_preserves_other_files(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            launcher = root / "project with spaces" / "UmbraCam.sh"
            launcher.parent.mkdir()
            launcher.touch()
            bin_dir, data_dir = root / "bin", root / "share"
            command, desktop = installer.install(bin_dir, data_dir, launcher)
            installer.install(bin_dir, data_dir, launcher)
            self.assertEqual(command.resolve(), launcher)
            self.assertIn(f'Exec="{launcher}" menu', desktop.read_text())
            other = bin_dir / "another-app"
            other.touch()
            installer.uninstall(bin_dir, data_dir, launcher)
            self.assertFalse(command.is_symlink())
            self.assertFalse(desktop.exists())
            self.assertTrue(other.exists())

    def test_existing_unrelated_command_is_preserved(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            (root / "umbracam").write_text("unrelated")
            with self.assertRaisesRegex(ValueError, "Refusing"):
                installer.install(root, root / "share")
            self.assertEqual((root / "umbracam").read_text(), "unrelated")

    def test_existing_unrelated_desktop_file_is_preserved(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            desktop = root / "share" / "applications" / installer.DESKTOP_NAME
            desktop.parent.mkdir(parents=True)
            desktop.write_text("unrelated")
            with self.assertRaisesRegex(ValueError, "Refusing"):
                installer.install(root / "bin", root / "share")
            self.assertEqual(desktop.read_text(), "unrelated")
            self.assertFalse((root / "bin").exists())

    def test_unsafe_desktop_control_characters_rejected(self):
        with self.assertRaises(ValueError):
            installer.desktop_entry(Path("/tmp/bad\npath"))

    def test_desktop_substitutions_are_escaped(self):
        entry = installer.desktop_entry(Path('/tmp/project % and $ and " quote/UmbraCam.sh'))
        self.assertIn("%%", entry)
        self.assertIn('\\\\$', entry)
        self.assertIn('\\\\"', entry)

    def test_symlinked_launcher_resolves_checkout_with_spaces(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            project = root / "project with spaces"
            (project / "linux").mkdir(parents=True)
            original = MODULE.parent.parent / "UmbraCam.sh"
            launcher = project / "UmbraCam.sh"
            launcher.write_text(original.read_text())
            launcher.chmod(0o755)
            (project / "linux" / "umbracam.py").write_text('print("correct checkout")\n')
            link = root / "umbracam"
            link.symlink_to(launcher)
            result = subprocess.run([str(link)], capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout.strip(), "correct checkout")


if __name__ == "__main__":
    unittest.main()
