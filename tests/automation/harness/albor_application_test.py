import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from albor_automation.albor_application import AlborApplication, _descriptor_in, _discovery_directories, default_executable
from albor_automation.locator import Locator


class FakeProcess:
    def __init__(self, hang_on_wait: bool = False, exit_code: int | None = None) -> None:
        self._hang_on_wait = hang_on_wait
        self._exit_code = exit_code
        self.returncode = exit_code
        self.pid = 4242
        self.terminated = 0
        self.killed = 0

    def poll(self) -> int | None:
        return self._exit_code

    def terminate(self) -> None:
        self.terminated += 1
        if not self._hang_on_wait:
            self._exit_code = 0
            self.returncode = 0

    def kill(self) -> None:
        self.killed += 1
        self._hang_on_wait = False
        self._exit_code = -9
        self.returncode = -9

    def wait(self, timeout: float | None = None) -> int:
        if self._hang_on_wait:
            raise subprocess.TimeoutExpired(cmd="fake-app", timeout=timeout)
        if self._exit_code is None:
            self._exit_code = 0
            self.returncode = 0
        return self.returncode


class FakeMcp:
    def __init__(self) -> None:
        self.closed = False

    def close(self) -> None:
        self.closed = True


class FakeClient:
    def __init__(self) -> None:
        self._mcp = FakeMcp()

    def call_tool(self, name: str, arguments: dict | None = None) -> str:
        return "group - \nbutton ok OK\n"

    def screenshot(self) -> bytes:
        return b"png-bytes"


class ApplicationInstanceChecks(unittest.TestCase):
    def setUp(self) -> None:
        self._scratch = tempfile.TemporaryDirectory()
        self.addCleanup(self._scratch.cleanup)
        self._stop_path = Path(self._scratch.name) / "stop"

    def _make_app(self, process: FakeProcess | None = None) -> tuple[AlborApplication, FakeProcess, FakeClient]:
        process = process or FakeProcess()
        client = FakeClient()
        app = AlborApplication(process, client, Path(self._scratch.name) / "host.json", self._stop_path, None, None, None)
        return app, process, client

    def test_client_and_descriptor_are_exposed(self) -> None:
        # arrange
        app, _, client = self._make_app()
        # assert
        self.assertIs(app.client(), client)
        self.assertEqual(app.descriptor_path(), Path(self._scratch.name) / "host.json")

    def test_is_running_follows_the_process(self) -> None:
        # arrange
        app, process, _ = self._make_app()
        # assert
        self.assertTrue(app.is_running())
        process._exit_code = 0
        self.assertFalse(app.is_running())

    def test_get_by_id_returns_a_locator(self) -> None:
        # arrange
        app, _, _ = self._make_app()
        # act
        locator = app.get_by_id("ok")
        # assert
        self.assertIsInstance(locator, Locator)
        self.assertEqual(locator.node_id(), "ok")

    def test_close_writes_the_stop_file_and_releases_the_transport(self) -> None:
        # arrange
        app, process, client = self._make_app()
        # act
        app.close()
        # assert
        self.assertEqual(self._stop_path.read_text(), "stop\n")
        self.assertEqual(process.terminated, 0)
        self.assertTrue(client._mcp.closed)
        self.assertFalse(app.is_running())

    def test_close_kills_a_process_that_ignores_the_stop_file(self) -> None:
        # arrange
        process = FakeProcess(hang_on_wait=True)
        app, _, client = self._make_app(process)
        # act
        app.close()
        # assert
        self.assertEqual(process.terminated, 1)
        self.assertEqual(process.killed, 1)
        self.assertTrue(client._mcp.closed)

    def test_context_manager_closes_on_exit(self) -> None:
        # arrange
        app, _, client = self._make_app()
        # act
        with app as entered:
            self.assertIs(entered, app)
        # assert
        self.assertTrue(client._mcp.closed)

    def test_collect_artifacts_writes_every_file(self) -> None:
        # arrange
        app, _, _ = self._make_app()
        descriptor = Path(self._scratch.name) / "host.json"
        descriptor.write_text('{"socket": "/tmp/mcp.sock"}')
        # act
        directory = app.collect_artifacts(str(Path(self._scratch.name) / "artifacts"))
        # assert
        root = Path(directory)
        self.assertEqual((root / "screenshot.png").read_bytes(), b"png-bytes")
        self.assertIn("button ok OK", (root / "tree.txt").read_text())
        self.assertIn("mcp.sock", (root / "descriptor.json").read_text())
        self.assertEqual((root / "stderr.txt").read_text(), "")


class DiscoveryChecks(unittest.TestCase):
    def test_descriptor_in_finds_the_serving_line(self) -> None:
        # assert
        self.assertEqual(_descriptor_in("nothing\nalbor automation: serving /tmp/42/host.json\n"), Path("/tmp/42/host.json"))

    def test_descriptor_in_returns_none_without_a_line(self) -> None:
        # assert
        self.assertIsNone(_descriptor_in("nothing"))

    def test_default_executable_uses_the_environment_override(self) -> None:
        # arrange
        with mock.patch.dict(os.environ, {"ALBOR_TEST_APPLICATION": "/opt/my/app"}):
            # assert
            self.assertEqual(default_executable(), "/opt/my/app")

    def test_discovery_directories_cover_the_platform_layout(self) -> None:
        # act
        directories = _discovery_directories(4242)
        # assert: the windows layout starts under the local application data directory
        if os.name == "nt":
            local = Path(os.environ["LOCALAPPDATA"])
            self.assertEqual(directories, [local / "albor-mcp" / "4242"])
        # assert: the posix layout covers the runtime and short fallback roots
        else:
            self.assertIn(Path("/tmp") / "albor-mcp" / "4242", directories)
            self.assertTrue(any(directory.name == "4242" for directory in directories))


if __name__ == "__main__":
    unittest.main()
