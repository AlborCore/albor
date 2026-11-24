import base64
import json
import time
from typing import Any

from .errors import McpError
from .log import logger
from .transport import LineTransport, default_transport

# the deadline for one reply, which a busy ui thread can delay
DEFAULT_READ_TIMEOUT = 30.0

# the protocol revision the embedded host advertises
MCP_PROTOCOL_VERSION = "2024-11-05"

# the logger that carries request and reply lines
_LOGGER = logger()


class McpClient:
    def __init__(self, endpoint: str, token: str | None, transport: LineTransport | None = None) -> None:
        # endpoint is the socket or pipe name the host published
        self._endpoint = endpoint
        # token is the session secret, where none means an unauthenticated handshake
        self._token = token
        # injected transport replaces the platform default in unit tests
        self._transport = transport
        # connected records whether the handshake succeeded
        self._connected = False
        # next_id is the json-rpc identifier of the following request
        self._next_id = 1
        # server_info captures the handshake answer for callers
        self._server_info: dict[str, Any] = {}

    def connect(self) -> None:
        # a second connect must not restart the conversation
        if self._connected:
            return
        # the platform transport is created unless a test supplied one
        if self._transport is None:
            self._transport = default_transport(self._endpoint)
        # connect opens the endpoint before any request is written
        self._transport.connect()
        # the handshake proves the client holds the session token
        result = self._handshake()
        # the advertised server information is kept for callers
        self._server_info = result.get("serverInfo", {})
        # only a successful handshake marks the connection ready
        self._connected = True

    def server_info(self) -> dict[str, Any]:
        # return the server information captured during the handshake
        return self._server_info

    def list_tools(self) -> list[dict[str, Any]]:
        # ask the host for its allow-listed tools
        result = self.call("tools/list")
        # the answer carries the list under the tools member
        return result["tools"]

    def call_tool(self, name: str, arguments: dict[str, str] | None = None) -> Any:
        # every tool call becomes exactly one json-rpc request
        result = self.call("tools/call", {"name": name, "arguments": arguments or {}})
        # a refused call is flagged in the result by the adapter
        if result.get("isError"):
            raise McpError(result["content"][0]["text"])
        # decode the content blocks into the caller's value
        return _decode_content(result["content"])

    def call(self, method: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
        # build one request envelope with the next sequential identifier
        request: dict[str, Any] = {"jsonrpc": "2.0", "id": self._next_id, "method": method}
        # consume the identifier so two calls never share one
        self._next_id += 1
        # attach parameters only when the method carries them
        if params is not None:
            request["params"] = params
        # expose the request for protocol diagnostics
        _LOGGER.debug("mcp request: %s", json.dumps(request))
        # one request and one reply make up the conversation
        reply = self._exchange(request)
        # surface a json-rpc error as a harness error
        if "error" in reply:
            error = reply["error"]
            raise McpError(f"json-rpc error {error.get('code')}: {error.get('message')}")
        # a successful reply carries a result object
        return reply["result"]

    def close(self) -> None:
        # reset the handshake state so a closed client cannot be reused
        self._connected = False
        # release the transport and its reader thread
        if self._transport is not None:
            self._transport.close()

    def _handshake(self) -> dict[str, Any]:
        # build the initialize parameters with the protocol and client name
        params: dict[str, Any] = {"protocolVersion": MCP_PROTOCOL_VERSION, "capabilities": {}, "clientInfo": {"name": "albor-automation-harness", "version": "0.1.0"}}
        # the token travels in the metadata key the host reads
        if self._token is not None:
            params["_meta"] = {"albor/token": self._token}
        # the handshake is an ordinary json-rpc request
        return self.call("initialize", params)

    def _exchange(self, request: dict[str, Any], timeout: float = DEFAULT_READ_TIMEOUT) -> dict[str, Any]:
        # a call without a transport cannot reach the host
        if self._transport is None:
            raise McpError("the mcp client is not connected")
        # frame the request as one line of json
        line = json.dumps(request)
        # write the request and wait for its matching reply
        self._transport.send_line(line)
        # the deadline bounds a host that never answers
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            # read one complete reply from the transport
            message = json.loads(self._transport.read_line(max(0.1, deadline - time.monotonic())))
            # notifications carry no matching identifier and are skipped
            if message.get("id") != request["id"]:
                continue
            # expose the reply for protocol diagnostics
            _LOGGER.debug("mcp reply: %s", json.dumps(message))
            # a reply is either a result or an error envelope
            if "result" in message or "error" in message:
                return message
        # a matching reply that never arrived is a bounded failure
        raise McpError(f"timed out waiting for the reply to request {request['id']}")


def _decode_content(content: list[dict[str, Any]]) -> Any:
    # text blocks carry either a scalar or a nested json document
    if content and content[0].get("type") == "text":
        # try the json document first, because bounds and values are typed
        try:
            return json.loads(content[0]["text"])
        except json.JSONDecodeError:
            # plain text is the fallback for names and empty answers
            return content[0]["text"]
    # image blocks carry a base64 encoded png payload
    if content and content[0].get("type") == "image":
        # the decoded bytes are the png the host presented
        return base64.b64decode(content[0]["data"])
    # binary blocks carry base64 bytes under a resource
    if content and content[0].get("type") == "resource":
        # the blob is the opaque payload the caller asked for
        return base64.b64decode(content[0]["resource"]["blob"])
    # an unknown block type is returned for the caller to inspect
    return content
