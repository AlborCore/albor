from typing import Any

from .log import logger

# trailing tree flags that end a name that contains spaces
_TRAILING_FLAGS = ("writable", "selection=", "read_only", "multiline", "expanded", "disabled", "selected", "focused", "indeterminate")

# the logger that records facade diagnostics
_LOGGER = logger()


class AlborClient:

    def __init__(self, mcp_client: Any) -> None:
        # mcp_client carries the authenticated json-rpc conversation
        self._mcp = mcp_client

    def close(self) -> None:
        # release the connection so another client can be served
        self._mcp.close()

    def server_info(self) -> dict[str, Any]:
        # expose the handshake answer
        return self._mcp.server_info()

    def list_tools(self) -> list[dict[str, Any]]:
        # expose the allow-listed tools the host advertised
        return self._mcp.list_tools()

    def call_tool(self, name: str, arguments: dict[str, str] | None = None) -> Any:
        # pass one command to the layer for the conformance suite
        return self._mcp.call_tool(name, arguments)

    def tree(self) -> list[dict[str, Any]]:
        # read the whole semantic tree from the layer
        text = self._mcp.call_tool("semantic.tree")
        # the dump is one node per line
        return parse_tree(text)

    def find(self, node_id: str) -> dict[str, Any] | None:
        # resolve an author assigned id against the live tree
        for node in self.tree():
            if node["id"] == node_id:
                return node
        # an absent id is reported as none rather than an error
        return None

    def invoke(self, node_id: str, action: str = "activate") -> None:
        # run one registered action on the node
        self._mcp.call_tool("semantic.invoke", {"id": node_id, "action": action})

    def set_value(self, node_id: str, value: str) -> None:
        # write the value an editable node edits
        self._mcp.call_tool("semantic.set_value", {"id": node_id, "value": value})

    def value(self, node_id: str) -> Any:
        # read the primary value, which the layer returns in its declared type
        return self._mcp.call_tool("semantic.value", {"id": node_id})

    def set_focused(self, node_id: str, focused: bool) -> None:
        # record what assistive technology reported about focus
        self._mcp.call_tool("semantic.set_focused", {"id": node_id, "value": "1" if focused else "0"})

    def bounds(self, node_id: str) -> dict[str, Any]:
        # read the arranged geometry of one node
        return self._mcp.call_tool("semantic.bounds", {"id": node_id})

    def screenshot(self) -> bytes:
        # capture the last presented frame as a png
        return self._mcp.call_tool("capture.screenshot", {"format": "png"})

    def input_click(self, node_id: str) -> None:
        # press a control through the pointer activation path
        self._mcp.call_tool("input.click", {"id": node_id})

    def input_type(self, node_id: str, text: str) -> None:
        # type a string into an editable control through its edit path
        self._mcp.call_tool("input.type", {"id": node_id, "text": text})

    def input_key(self, key: str, modifiers: str | None = None) -> None:
        # press one normalized key with optional modifiers
        if modifiers is None:
            self._mcp.call_tool("input.key", {"key": key})
        else:
            self._mcp.call_tool("input.key", {"key": key, "modifiers": modifiers})

    def input_pointer(self, node_id: str, action: str, x: str | None = None, y: str | None = None) -> None:
        # deliver one pointer event, omitting trailing coordinates when absent
        if x is None or y is None:
            self._mcp.call_tool("input.pointer", {"id": node_id, "action": action})
        else:
            self._mcp.call_tool("input.pointer", {"id": node_id, "action": action, "x": x, "y": y})


def parse_tree(text: str) -> list[dict[str, Any]]:
    # parse the dump into one dictionary per non empty line
    nodes: list[dict[str, Any]] = []
    for line in text.splitlines():
        # blank lines carry no node and are skipped
        if not line.strip():
            continue
        # each remaining line is one node
        nodes.append(parse_node(line))
    # the parsed tree is returned whole
    return nodes


def parse_node(line: str) -> dict[str, Any]:
    # parse one line as role, id, name, then trailing state flags
    role, _, remainder = line.partition(" ")
    node_id, _, remainder = remainder.partition(" ")
    node: dict[str, Any] = {"role": role, "id": node_id}
    # a value flag ends a name that itself contains spaces
    before, found, tail = _find_boundary(remainder, ("value=",))
    if found == "value=":
        node["name"] = _unescape(before)
        tail = tail[len("value="):]
        value_text, _, flag_tail = _find_boundary(tail, _TRAILING_FLAGS)
        node["value"] = _unescape(value_text)
        _parse_flags(node, flag_tail)
    else:
        before, _, flag_tail = _find_boundary(remainder, _TRAILING_FLAGS)
        node["name"] = _unescape(before)
        _parse_flags(node, flag_tail)
    return node


def _find_boundary(text: str, needles: tuple[str, ...]) -> tuple[str, str, str]:
    # find the first unescaped space delimited needle in the text
    index = 0
    while index < len(text):
        # an escaped character cannot open a boundary
        if text[index] == "\\" and index + 1 < len(text):
            index += 2
            continue
        # check every candidate at this position
        for candidate in needles:
            # a needle must match at the current index
            if not text.startswith(candidate, index):
                continue
            # a needle must start a token, not sit inside a word
            if index > 0 and text[index - 1] != " ":
                continue
            return text[:index].rstrip(" "), candidate, text[index:]
        # advance to the next character
        index += 1
    # no boundary means the whole text belongs to the caller
    return text, "", ""


def _parse_flags(node: dict[str, Any], text: str) -> None:
    # walk the trailing tokens into flags on the node
    index = 0
    while index < len(text):
        # skip the spaces between tokens
        if text[index] == " ":
            index += 1
            continue
        if text.startswith("selection=", index):
            index += len("selection=")
            value, index = _read_token(text, index)
            start, _, end = value.partition(":")
            node["selection"] = (int(start), int(end))
        elif text.startswith("value=", index):
            index += len("value=")
            value, index = _read_token(text, index)
            node["value"] = _unescape(value)
        elif text.startswith("writable", index):
            index += len("writable")
            node["writable"] = True
        elif text.startswith("read_only", index):
            index += len("read_only")
            node["read_only"] = True
        elif text.startswith("multiline", index):
            index += len("multiline")
            node["multiline"] = True
        elif text.startswith("expanded", index):
            index += len("expanded")
            node["expanded"] = True
        elif text.startswith("disabled", index):
            index += len("disabled")
            node["disabled"] = True
        elif text.startswith("selected", index):
            index += len("selected")
            node["selected"] = True
        elif text.startswith("focused", index):
            index += len("focused")
            node["focused"] = True
        elif text.startswith("indeterminate", index):
            index += len("indeterminate")
            node["indeterminate"] = True
        else:
            # an unknown token is skipped so a newer flag cannot wedge the parser
            _, index = _read_token(text, index)


def _read_token(text: str, index: int) -> tuple[str, int]:
    # read one space delimited token from the given index
    end = index
    while end < len(text) and text[end] != " ":
        end += 1
    return text[index:end], end


def _unescape(text: str) -> str:
    # decode the four control escapes the server writes
    result = ""
    index = 0
    while index < len(text):
        char = text[index]
        if char == "\\" and index + 1 < len(text):
            next_char = text[index + 1]
            result += {"n": "\n", "r": "\r", "t": "\t"}.get(next_char, next_char)
            index += 2
        else:
            result += char
            index += 1
    return result
