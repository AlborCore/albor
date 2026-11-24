import unittest
from typing import Any

from albor_automation.albor_client import AlborClient


class FakeMcpClient:
    def __init__(self) -> None:
        self.calls: list[tuple[str, dict[str, str]]] = []
        self.replies: dict[str, Any] = {}
        self.info: dict[str, Any] = {"name": "fake-albor"}
        self.tools: list[dict[str, Any]] = [{"name": "semantic.tree"}]

    def server_info(self) -> dict[str, Any]:
        return self.info

    def list_tools(self) -> list[dict[str, Any]]:
        return self.tools

    def call_tool(self, name: str, arguments: dict[str, str] | None = None) -> Any:
        self.calls.append((name, dict(arguments or {})))
        outcome = self.replies.get(name)
        if isinstance(outcome, Exception):
            raise outcome
        return outcome


class AlborClientFacadeChecks(unittest.TestCase):
    def setUp(self) -> None:
        self._mcp = FakeMcpClient()
        self._client = AlborClient(self._mcp)

    def test_server_info_is_forwarded(self) -> None:
        # assert
        self.assertEqual(self._client.server_info()["name"], "fake-albor")

    def test_list_tools_is_forwarded(self) -> None:
        # assert
        self.assertEqual([tool["name"] for tool in self._client.list_tools()], ["semantic.tree"])

    def test_tree_parses_the_dump(self) -> None:
        # arrange
        self._mcp.replies["semantic.tree"] = "group - \nbutton ok OK\n"
        # act
        nodes = self._client.tree()
        # assert
        self.assertEqual([node["role"] for node in nodes], ["group", "button"])

    def test_find_resolves_an_exact_id(self) -> None:
        # arrange
        self._mcp.replies["semantic.tree"] = "button form_press OK\nbutton other No"
        # act
        node = self._client.find("form_press")
        # assert
        self.assertEqual(node["name"], "OK")

    def test_find_returns_none_when_absent(self) -> None:
        # arrange
        self._mcp.replies["semantic.tree"] = "button form_press OK"
        # assert
        self.assertIsNone(self._client.find("missing"))

    def test_value_reads_the_primary_value(self) -> None:
        # arrange
        self._mcp.replies["semantic.value"] = "0.500000"
        # assert
        self.assertEqual(self._client.value("form_level"), "0.500000")

    def test_bounds_reads_the_geometry(self) -> None:
        # arrange
        self._mcp.replies["semantic.bounds"] = {"x": 16, "y": 76, "width": 388, "height": 32}
        # assert
        self.assertEqual(self._client.bounds("form_press")["width"], 388)

    def test_screenshot_reads_png_bytes(self) -> None:
        # arrange
        self._mcp.replies["capture.screenshot"] = b"\x89PNG"
        # assert
        self.assertEqual(self._client.screenshot(), b"\x89PNG")

    def test_input_click_sends_one_command(self) -> None:
        # act
        self._client.input_click("form_press")
        # assert
        self.assertEqual(self._mcp.calls, [("input.click", {"id": "form_press"})])

    def test_input_type_sends_the_text(self) -> None:
        # act
        self._client.input_type("form_name", "Ada Lovelace")
        # assert
        self.assertEqual(self._mcp.calls, [("input.type", {"id": "form_name", "text": "Ada Lovelace"})])

    def test_input_key_omits_absent_modifiers(self) -> None:
        # act
        self._client.input_key("return")
        # assert
        self.assertEqual(self._mcp.calls, [("input.key", {"key": "return"})])

    def test_input_key_sends_modifiers(self) -> None:
        # act
        self._client.input_key("s", "control")
        # assert
        self.assertEqual(self._mcp.calls, [("input.key", {"key": "s", "modifiers": "control"})])

    def test_input_pointer_sends_the_point(self) -> None:
        # act
        self._client.input_pointer("form_level", "press", "113", "250")
        # assert
        self.assertEqual(self._mcp.calls, [("input.pointer", {"id": "form_level", "action": "press", "x": "113", "y": "250"})])

    def test_input_pointer_omits_absent_coordinates(self) -> None:
        # act
        self._client.input_pointer("form_level", "release")
        # assert
        self.assertEqual(self._mcp.calls, [("input.pointer", {"id": "form_level", "action": "release"})])

    def test_call_tool_passes_the_command_through(self) -> None:
        # act
        self._client.call_tool("semantic.find", {"id": "form_press"})
        # assert
        self.assertEqual(self._mcp.calls, [("semantic.find", {"id": "form_press"})])


if __name__ == "__main__":
    unittest.main()
