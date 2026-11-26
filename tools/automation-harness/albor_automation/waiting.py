import functools
import time
from pathlib import Path
from types import TracebackType
from typing import Callable, ParamSpec, TypeVar

from .errors import AlborAssertionError
from .log import logger

# the default budget for a polling wait
DEFAULT_WAIT_TIMEOUT = 5.0

# the default sleep between two polls
DEFAULT_POLL_INTERVAL = 0.1

# the package directory hidden from reported tracebacks
_FRAMEWORK_PATH = str(Path(__file__).resolve().parent)

# the parameter types a decorated entry point forwards
_P = ParamSpec("_P")

# the result type a decorated entry point returns
_R = TypeVar("_R")

# the logger used when a wait gives up
_LOGGER = logger()


def reports_test_frames(function: Callable[_P, _R]) -> Callable[_P, _R]:
    # hide framework frames when an assertion leaves this package
    @functools.wraps(function)
    def wrapper(*args: _P.args, **kwargs: _P.kwargs) -> _R:
        # run the entry point and let non-assertion failures propagate
        try:
            return function(*args, **kwargs)
        except AlborAssertionError as error:
            # drop the frames between the caller and the raise site
            _trim_framework_frames(error)
            # re-raise with a traceback that points at the test
            raise
    return wrapper


def wait_for(condition: Callable[[], bool], *, timeout: float = DEFAULT_WAIT_TIMEOUT, poll_interval: float = DEFAULT_POLL_INTERVAL, timeout_message: str, observe: Callable[[], str] | None = None) -> None:
    # bound the poll by a monotonic deadline
    deadline = time.monotonic() + timeout
    while True:
        # evaluate the caller's condition once per iteration
        if condition():
            return
        # stop polling once the deadline passed
        if time.monotonic() >= deadline:
            break
        # yield between polls so the application can advance a frame
        time.sleep(poll_interval)
    # read the last observable state for the failure message
    state = _safe_observe(observe)
    # report the timeout with the observed state attached
    _LOGGER.debug("wait timed out: %s", timeout_message)
    # build the failure inside the helper so its frames can be trimmed
    try:
        raise AlborAssertionError(f"{timeout_message}\nlast observed: {state}\nwaited: {timeout} seconds")
    except AlborAssertionError as error:
        # hide the framework frames before the failure reaches the test
        _trim_framework_frames(error)
        # re-raise the prepared failure
        raise


def _trim_framework_frames(error: AlborAssertionError) -> None:
    # collect the traceback entries from oldest to newest
    frames = []
    entry = error.__traceback__
    while entry is not None:
        frames.append(entry)
        entry = entry.tb_next
    # keep the frames that belong to the test rather than this package
    kept = [entry for entry in frames if not entry.tb_frame.f_code.co_filename.startswith(_FRAMEWORK_PATH)]
    # rebuild the chain from the newest kept entry backwards
    previous = None
    for entry in reversed(kept):
        previous = TracebackType(previous, entry.tb_frame, entry.tb_lasti, entry.tb_lineno)
    # attach the trimmed chain to the exception
    error.__traceback__ = previous


def _safe_observe(observe: Callable[[], str] | None) -> str:
    # report an unknown state when the caller supplied no observer
    if observe is None:
        return "unknown"
    # observation must never mask the timeout failure
    try:
        return observe()
    except Exception:
        return "unknown"
