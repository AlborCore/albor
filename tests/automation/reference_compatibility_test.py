import json
import os
import struct
import unittest
from pathlib import Path

from albor_automation import McpClient, McpError, TestSession, expect, launch
from albor_automation.transport import default_transport

# the caller's executable wins, with this repository's built fixture for plain discovery
_REFERENCE_APP = Path(os.environ.get("ALBOR_TEST_APPLICATION") or Path(__file__).resolve().parents[2] / ".build-debug" / "tests" / "albor-automation-reference-app")
if os.name == "nt":
    _REFERENCE_APP = _REFERENCE_APP.with_suffix(".exe")

# the tool names the standard host permits (§10.3)
_STANDARD_TOOLS = {"semantic.tree", "semantic.find", "semantic.value", "semantic.bounds", "semantic.read", "semantic.invoke", "semantic.actions", "semantic.set_value", "semantic.set_focused", "input.type", "input.click", "input.pointer", "input.key", "input.wheel", "command.list", "command.get", "command.invoke", "command.can_undo", "command.undo", "command.redo", "capture.screenshot"}

# the value the form slider starts at
_INITIAL_LEVEL = 0.5

# the signature every png the host captures carries
_PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class AutomationCompatibilityChecks(unittest.TestCase):

    def test_discovery_and_handshake_reach_the_host(self) -> None:
        # arrange: launch the fixture application in an isolated session
        with TestSession(launch(str(_REFERENCE_APP)), self.id()) as app:
            # assert: the host published a descriptor with the mcp protocol
            descriptor = json.loads(app.descriptor_path().read_text())
            self.assertEqual(descriptor["protocol"], "mcp")
            self.assertEqual(descriptor["protocolVersion"], "2024-11-05")
            self.assertTrue(Path(descriptor["token"]).is_file())
            # assert: the client completed the handshake with that token
            self.assertEqual(app.client().server_info()["name"], "albor")

    def test_wrong_and_missing_tokens_are_refused(self) -> None:
        # arrange: launch without a client so the one host connection is free
        with TestSession(launch(str(_REFERENCE_APP), connect=False), self.id()) as app:
            descriptor = json.loads(app.descriptor_path().read_text())
            endpoint = descriptor["socket"]
            self.assertTrue(Path(descriptor["token"]).is_file())
            # act
            wrong = McpClient(endpoint, "not-the-session-token")
            try:
                with self.assertRaises(McpError) as context:
                    wrong.connect()
                self.assertIn("authentication failed", str(context.exception))
            finally:
                wrong.close()
            # act
            missing = McpClient(endpoint, None)
            try:
                with self.assertRaises(McpError) as context:
                    missing.connect()
                self.assertIn("authentication failed", str(context.exception))
            finally:
                missing.close()

    def test_a_request_before_handshake_is_refused(self) -> None:
        # arrange: launch without a client and open the transport directly
        with TestSession(launch(str(_REFERENCE_APP), connect=False), self.id()) as app:
            descriptor = json.loads(app.descriptor_path().read_text())
            transport = default_transport(descriptor["socket"])
            transport.connect()
            client = McpClient(descriptor["socket"], None, transport)
            # assert: the post-handshake guard refuses a request on a fresh connection
            with self.assertRaises(McpError) as context:
                client.list_tools()
            self.assertIn("authentication failed", str(context.exception))
            # cleanup: release the direct transport
            transport.close()

    def test_a_second_client_can_connect_after_the_first_closes(self) -> None:
        # arrange: launch without a client so both connections use the real host
        with TestSession(launch(str(_REFERENCE_APP), connect=False), self.id()) as app:
            descriptor = json.loads(app.descriptor_path().read_text())
            token = Path(descriptor["token"]).read_text().strip()
            for attempt in range(2):
                client = McpClient(descriptor["socket"], token)
                try:
                    client.connect()
                    self.assertEqual(client.server_info()["name"], "albor")
                finally:
                    client.close()

    def test_allow_list_limits_the_tool_surface(self) -> None:
        # arrange: launch the fixture application
        with TestSession(launch(str(_REFERENCE_APP)), self.id()) as app:
            # act: read the advertised tools
            tools = app.client().list_tools()
            names = {tool["name"] for tool in tools}
            # assert: the standard surface is exactly the permitted set
            self.assertEqual(names, _STANDARD_TOOLS)
            self.assertEqual(len(tools), 21)
            # assert
            with self.assertRaises(McpError) as context:
                app.client().call_tool("internal.hidden")
            self.assertIn("no such command: internal.hidden", str(context.exception))

    def test_reference_session_drives_the_sample_form(self) -> None:
        # arrange: launch the generated form through the real host
        with TestSession(launch(str(_REFERENCE_APP)), self.id()) as app:
            client = app.client()
            # assert: the title button is reachable by its author assigned id
            self.assertEqual(client.call_tool("semantic.find", {"id": "form_press"}), "button press me")
            bounds = client.bounds("form_press")
            self.assertGreater(bounds["width"], 0)
            self.assertGreater(bounds["y"], 0)
            # assert: the typed reads answer the sample's starting values
            self.assertAlmostEqual(float(client.value("form_level")), _INITIAL_LEVEL)
            self.assertEqual(client.value("form_armed"), "no")
            self.assertEqual(client.value("form_name"), "")
            # act: type into the field through the keyboard edit path
            app.get_by_id("form_name").type_text("Ada Lovelace")
            # assert: the typed value is what the field now holds
            expect(app.get_by_id("form_name")).to_have_value("Ada Lovelace")
            # act: click the button and then press return while it is focused
            app.get_by_id("form_press").click_input()
            expect(app.get_by_id("form_presses")).to_have_name("presses 1")
            client.input_key("return")
            expect(app.get_by_id("form_presses")).to_have_name("presses 2")
            # act: drag the slider from a quarter to three quarters across
            slider = client.bounds("form_level")
            track_y = str(slider["y"] + slider["height"] * 0.5)
            press_x = str(slider["x"] + slider["width"] * 0.25)
            move_x = str(slider["x"] + slider["width"] * 0.75)
            client.input_pointer("form_level", "press", press_x, track_y)
            client.input_pointer("form_level", "move", move_x, track_y)
            client.input_pointer("form_level", "release", move_x, track_y)
            # assert: the drag moved the value past its midpoint
            app.wait_for_condition(lambda: float(client.value("form_level")) > _INITIAL_LEVEL, timeout=5.0, message="expected the drag to move the slider past its midpoint")
            # assert: the frame is a png with positive dimensions in its header
            png = client.screenshot()
            self.assertTrue(png.startswith(_PNG_SIGNATURE))
            self.assertEqual(png[12:16], b"IHDR")
            self.assertGreater(struct.unpack(">II", png[16:24])[0], 0)
            self.assertGreater(struct.unpack(">II", png[16:24])[1], 0)
            # assert
            with self.assertRaises(McpError) as context:
                client.call_tool("semantic.find", {"id": "form_missing"})
            self.assertIn("no node with id: form_missing", str(context.exception))


if __name__ == "__main__":
    unittest.main()
