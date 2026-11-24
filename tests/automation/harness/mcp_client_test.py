import json
import unittest

from albor_automation.errors import McpError
from albor_automation.mcp_client import McpClient

# the identifier the first handshake request carries
_FIRST_ID = 1


class FakeTransport:
    def __init__(self, replies: list[str] | None = None) -> None:
        self.replies = list(replies or [])
        self.sent: list[str] = []
        self.closed = False

    def connect(self) -> None:
        self.connected = True

    def send_line(self, line: str) -> None:
        self.sent.append(line)

    def read_line(self, timeout: float) -> str:
        if not self.replies:
            raise McpError("no reply within 0.1 seconds")
        return self.replies.pop(0)

    def close(self) -> None:
        self.closed = True


def rpc_result(request_id: int, result: dict) -> str:
    return json.dumps({"jsonrpc": "2.0", "id": request_id, "result": result})


def rpc_error(request_id: int, code: int, message: str) -> str:
    return json.dumps({"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}})


def initialize_reply() -> str:
    return rpc_result(_FIRST_ID, {"protocolVersion": "2024-11-05", "serverInfo": {"name": "albor"}})


class McpClientHandshakeChecks(unittest.TestCase):
    def test_connect_sends_the_token_in_metadata(self) -> None:
        # arrange
        transport = FakeTransport([initialize_reply()])
        client = McpClient("/tmp/mcp.sock", "the-token", transport)
        # act
        client.connect()
        # assert
        request = json.loads(transport.sent[0])
        self.assertEqual(request["method"], "initialize")
        self.assertEqual(request["params"]["protocolVersion"], "2024-11-05")
        self.assertEqual(request["params"]["_meta"]["albor/token"], "the-token")

    def test_connect_without_token_omits_metadata(self) -> None:
        # arrange
        transport = FakeTransport([initialize_reply()])
        client = McpClient("/tmp/mcp.sock", None, transport)
        # act
        client.connect()
        # assert
        self.assertNotIn("_meta", json.loads(transport.sent[0])["params"])

    def test_connect_keeps_the_server_info(self) -> None:
        # arrange
        transport = FakeTransport([initialize_reply()])
        client = McpClient("/tmp/mcp.sock", "the-token", transport)
        # act
        client.connect()
        # assert
        self.assertEqual(client.server_info()["name"], "albor")

    def test_connect_is_idempotent(self) -> None:
        # arrange
        transport = FakeTransport([initialize_reply()])
        client = McpClient("/tmp/mcp.sock", "the-token", transport)
        # act
        client.connect()
        client.connect()
        # assert
        self.assertEqual(len(transport.sent), 1)

    def test_authentication_error_is_reported(self) -> None:
        # arrange
        transport = FakeTransport([rpc_error(_FIRST_ID, -32000, "authentication failed")])
        client = McpClient("/tmp/mcp.sock", "wrong", transport)
        # assert
        with self.assertRaises(McpError) as context:
            client.connect()
        self.assertIn("authentication failed", str(context.exception))


class McpClientCallChecks(unittest.TestCase):
    def setUp(self) -> None:
        self._transport = FakeTransport([initialize_reply()])
        self._client = McpClient("/tmp/mcp.sock", "the-token", self._transport)
        self._client.connect()

    def test_tool_call_is_one_request(self) -> None:
        # arrange
        self._transport.replies.append(rpc_result(_FIRST_ID + 1, {"content": [{"type": "text", "text": ""}], "isError": False}))
        # act
        self._client.call_tool("input.click", {"id": "form_press"})
        # assert
        request = json.loads(self._transport.sent[1])
        self.assertEqual(request["method"], "tools/call")
        self.assertEqual(request["params"]["name"], "input.click")
        self.assertEqual(request["params"]["arguments"], {"id": "form_press"})

    def test_json_text_is_decoded(self) -> None:
        # arrange
        self._transport.replies.append(rpc_result(_FIRST_ID + 1, {"content": [{"type": "text", "text": '{"x":16,"y":76}'}], "isError": False}))
        # act
        value = self._client.call_tool("semantic.bounds")
        # assert
        self.assertEqual(value["x"], 16)

    def test_plain_text_is_kept(self) -> None:
        # arrange
        self._transport.replies.append(rpc_result(_FIRST_ID + 1, {"content": [{"type": "text", "text": "button press me"}], "isError": False}))
        # act
        value = self._client.call_tool("semantic.find")
        # assert
        self.assertEqual(value, "button press me")

    def test_image_block_is_decoded(self) -> None:
        # arrange
        self._transport.replies.append(rpc_result(_FIRST_ID + 1, {"content": [{"type": "image", "data": "aGVsbG8=", "mimeType": "image/png"}], "isError": False}))
        # act
        value = self._client.call_tool("capture.screenshot")
        # assert
        self.assertEqual(value, b"hello")

    def test_error_reply_is_reported(self) -> None:
        # arrange
        self._transport.replies.append(rpc_error(_FIRST_ID + 1, -32000, "no such command: hidden"))
        # assert
        with self.assertRaises(McpError) as context:
            self._client.call_tool("hidden")
        self.assertIn("no such command: hidden", str(context.exception))

    def test_notifications_are_skipped(self) -> None:
        # arrange
        self._transport.replies.append(json.dumps({"jsonrpc": "2.0", "method": "notifications/message"}))
        self._transport.replies.append(rpc_result(_FIRST_ID + 1, {"content": [{"type": "text", "text": ""}], "isError": False}))
        # act
        result = self._client.call("tools/list")
        # assert
        self.assertEqual(result["content"][0]["text"], "")

    def test_close_releases_the_transport(self) -> None:
        # act
        self._client.close()
        # assert
        self.assertTrue(self._transport.closed)


if __name__ == "__main__":
    unittest.main()
