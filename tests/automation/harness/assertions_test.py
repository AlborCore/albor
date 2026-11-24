import unittest
from typing import Any

from albor_automation.assertions import expect
from albor_automation.errors import AlborAssertionError
from albor_automation.locator import Locator


class FakeClient:
    def __init__(self, nodes: list[dict[str, Any]]) -> None:
        self._nodes = nodes

    def find(self, node_id: str) -> dict[str, Any] | None:
        for node in self._nodes:
            if node["id"] == node_id:
                return dict(node)
        return None


class FakeApp:
    def __init__(self, nodes: list[dict[str, Any]]) -> None:
        self._client = FakeClient(nodes)

    def client(self) -> FakeClient:
        return self._client

    def tree_text(self) -> str:
        return "tree dump"


class ExpectChecks(unittest.TestCase):
    def test_to_exist_passes_when_present(self) -> None:
        # arrange
        locator = Locator(FakeApp([{"role": "button", "id": "ok", "name": "OK"}]), "ok")
        # act
        expect(locator).to_exist()

    def test_to_exist_times_out_when_missing(self) -> None:
        # arrange
        locator = Locator(FakeApp([]), "missing")
        # assert
        with self.assertRaises(AlborAssertionError) as context:
            expect(locator).to_exist(timeout=0.2, poll_interval=0.05)
        self.assertIn("expected node id=missing (not found) to exist", str(context.exception))

    def test_not_to_exist_passes_when_absent(self) -> None:
        # arrange
        locator = Locator(FakeApp([]), "missing")
        # act
        expect(locator).not_().to_exist()

    def test_not_to_exist_times_out_when_present(self) -> None:
        # arrange
        locator = Locator(FakeApp([{"role": "button", "id": "ok", "name": "OK"}]), "ok")
        # assert
        with self.assertRaises(AlborAssertionError) as context:
            expect(locator).not_().to_exist(timeout=0.2, poll_interval=0.05)
        self.assertIn("to NOT exist", str(context.exception))

    def test_to_have_name_passes_on_the_announced_name(self) -> None:
        # arrange
        locator = Locator(FakeApp([{"role": "button", "id": "ok", "name": "OK"}]), "ok")
        # act
        expect(locator).to_have_name("OK")

    def test_to_have_value_passes_on_the_editable_value(self) -> None:
        # arrange
        locator = Locator(FakeApp([{"role": "textfield", "id": "name", "name": "Name", "value": "Ada"}]), "name")
        # act
        expect(locator).to_have_value("Ada")

    def test_to_be_writable_passes_on_the_flag(self) -> None:
        # arrange
        locator = Locator(FakeApp([{"role": "textfield", "id": "name", "name": "Name", "writable": True}]), "name")
        # act
        expect(locator).to_be_writable()

    def test_to_have_state_passes_on_the_flag(self) -> None:
        # arrange
        locator = Locator(FakeApp([{"role": "button", "id": "ok", "name": "OK", "disabled": True}]), "ok")
        # act
        expect(locator).to_have_state("disabled")


if __name__ == "__main__":
    unittest.main()
