import unittest
from typing import Any

from albor_automation.errors import AlborAssertionError
from albor_automation.locator import Locator, LocatorError


class FakeClient:

    def __init__(self, nodes: list[dict[str, Any]]) -> None:
        self._nodes = nodes
        self.invokes: list[tuple[str, str]] = []
        self.values: list[tuple[str, str]] = []
        self.clicks: list[str] = []
        self.typed: list[tuple[str, str]] = []
        self.bounded = ""

    def find(self, node_id: str) -> dict[str, Any] | None:
        for node in self._nodes:
            if node["id"] == node_id:
                return dict(node)
        return None

    def invoke(self, node_id: str, action: str = "activate") -> None:
        self.invokes.append((node_id, action))

    def set_value(self, node_id: str, value: str) -> None:
        self.values.append((node_id, value))

    def input_click(self, node_id: str) -> None:
        self.clicks.append(node_id)

    def input_type(self, node_id: str, text: str) -> None:
        self.typed.append((node_id, text))

    def bounds(self, node_id: str) -> dict[str, Any]:
        self.bounded = node_id
        return {"x": 12, "y": 12, "width": 24, "height": 20}


class FakeApp:

    def __init__(self, nodes: list[dict[str, Any]]) -> None:
        self._client = FakeClient(nodes)
        self.tree_calls = 0

    def client(self) -> FakeClient:
        return self._client

    def tree_text(self) -> str:
        self.tree_calls += 1
        return "tree dump"


class LocatorChecks(unittest.TestCase):

    def test_exists_reports_presence(self) -> None:
        # arrange
        app = FakeApp([{"role": "button", "id": "ok", "name": "OK"}])
        # assert
        self.assertTrue(Locator(app, "ok").exists())

    def test_exists_reports_absence(self) -> None:
        # arrange
        app = FakeApp([{"role": "button", "id": "ok", "name": "OK"}])
        # assert
        self.assertFalse(Locator(app, "missing").exists())

    def test_node_raises_when_absent(self) -> None:
        # arrange
        locator = Locator(FakeApp([]), "missing")
        # assert
        with self.assertRaises(LocatorError) as context:
            locator.node()
        self.assertIn("no node with id 'missing'", str(context.exception))

    def test_value_defaults_to_empty(self) -> None:
        # arrange
        locator = Locator(FakeApp([{"role": "button", "id": "ok", "name": "OK"}]), "ok")
        # assert
        self.assertEqual(locator.value(), "")

    def test_writable_reflects_the_flag(self) -> None:
        # arrange
        locator = Locator(FakeApp([{"role": "textfield", "id": "path", "name": "path", "writable": True}]), "path")
        # assert
        self.assertTrue(locator.writable())

    def test_click_invokes_the_default_action(self) -> None:
        # arrange
        app = FakeApp([{"role": "button", "id": "ok", "name": "OK"}])
        # act
        Locator(app, "ok").click()
        # assert
        self.assertEqual(app.client().invokes, [("ok", "activate")])

    def test_click_input_uses_the_pointer_path(self) -> None:
        # arrange
        app = FakeApp([{"role": "button", "id": "ok", "name": "OK"}])
        # act
        Locator(app, "ok").click_input()
        # assert
        self.assertEqual(app.client().clicks, ["ok"])

    def test_type_text_uses_the_keyboard_path(self) -> None:
        # arrange
        app = FakeApp([{"role": "textfield", "id": "name", "name": "Name"}])
        # act
        Locator(app, "name").type_text("Ada")
        # assert
        self.assertEqual(app.client().typed, [("name", "Ada")])

    def test_fill_writes_the_value(self) -> None:
        # arrange
        app = FakeApp([{"role": "textfield", "id": "path", "name": "path"}])
        # act
        Locator(app, "path").fill("/bin/echo")
        # assert
        self.assertEqual(app.client().values, [("path", "/bin/echo")])

    def test_describe_includes_role_id_and_name(self) -> None:
        # arrange
        locator = Locator(FakeApp([{"role": "button", "id": "ok", "name": "OK"}]), "ok")
        # assert
        self.assertEqual(locator.describe(), "button id=ok name='OK'")

    def test_describe_reports_absence(self) -> None:
        # arrange
        locator = Locator(FakeApp([]), "missing")
        # assert
        self.assertEqual(locator.describe(), "id=missing (not found)")

    def test_wait_for_exists_times_out_with_the_tree(self) -> None:
        # arrange
        locator = Locator(FakeApp([]), "missing")
        # assert
        with self.assertRaises(AlborAssertionError) as context:
            locator.wait_for_exists(timeout=0.2, poll_interval=0.05)
        self.assertIn("node 'missing' did not appear", str(context.exception))
        self.assertIn("last observed: tree dump", str(context.exception))

    def test_wait_for_gone_passes_when_absent(self) -> None:
        # arrange
        locator = Locator(FakeApp([]), "missing")
        # act
        locator.wait_for_gone(timeout=0.2)

    def test_bounds_reads_the_resolved_node(self) -> None:
        # arrange
        app = FakeApp([{"role": "button", "id": "main_open", "name": "Open"}])
        # act
        bounds = Locator(app, "main_open").bounds()
        # assert
        self.assertEqual(bounds["height"], 20)
        self.assertEqual(app.client().bounded, "main_open")


if __name__ == "__main__":
    unittest.main()
