from typing import Any

from .errors import LocatorError
from .log import logger
from .waiting import DEFAULT_POLL_INTERVAL, DEFAULT_WAIT_TIMEOUT, wait_for

# the logger that records a failed resolution
_LOGGER = logger()


class Locator:

    def __init__(self, app: Any, node_id: str) -> None:
        # app is the application whose live tree is resolved
        self._app = app
        # node_id is the author assigned identity automation addresses
        self._node_id = node_id

    def node_id(self) -> str:
        # return the author assigned id this locator selects
        return self._node_id

    def describe(self) -> str:
        # build the human readable description failures use
        node = self._find()
        if node is None:
            return f"id={self._node_id} (not found)"
        # role and name make a mismatch obvious
        return f"{node['role']} id={node['id']} name={node['name']!r}"

    def exists(self) -> bool:
        # report whether the node is present in the live tree
        return self._find() is not None

    def wait_for_exists(self, timeout: float = DEFAULT_WAIT_TIMEOUT, poll_interval: float = DEFAULT_POLL_INTERVAL) -> None:
        # poll until the id appears in the live tree
        wait_for(self.exists, timeout=timeout, poll_interval=poll_interval, timeout_message=f"node {self._node_id!r} did not appear", observe=self._app.tree_text)

    def wait_for_gone(self, timeout: float = DEFAULT_WAIT_TIMEOUT, poll_interval: float = DEFAULT_POLL_INTERVAL) -> None:
        # poll until the id disappears from the live tree
        wait_for(lambda: not self.exists(), timeout=timeout, poll_interval=poll_interval, timeout_message=f"node {self._node_id!r} did not disappear", observe=self._app.tree_text)

    def node(self) -> dict[str, Any]:
        # return the resolved node or raise when it is absent
        node = self._find()
        if node is None:
            raise LocatorError(f"no node with id {self._node_id!r}")
        return node

    def text(self) -> str:
        # read the announced name of the node
        return self.node()["name"]

    def value(self) -> str:
        # read the editable value, which is empty when the node edits none
        return self.node().get("value", "")

    def writable(self) -> bool:
        # report whether the node accepts a value write
        return self.node().get("writable", False)

    def bounds(self) -> dict[str, Any]:
        # read the geometry the node occupies
        return self._app.client().bounds(self._node_id)

    def click(self, action: str = "activate") -> None:
        # run one registered action on the node
        self._app.client().invoke(self._node_id, action)

    def click_input(self) -> None:
        # press the node through the pointer activation path
        self._app.client().input_click(self._node_id)

    def fill(self, text: str) -> None:
        # write the value the editable node edits
        self._app.client().set_value(self._node_id, text)

    def type_text(self, text: str) -> None:
        # type into the node through the keyboard edit path
        self._app.client().input_type(self._node_id, text)

    def _find(self) -> dict[str, Any] | None:
        # resolve the id against the live tree without raising
        try:
            return self._app.client().find(self._node_id)
        except Exception:
            # a transport hiccup reads as absence so polling can retry
            _LOGGER.debug("node %s could not be resolved", self._node_id)
            return None
