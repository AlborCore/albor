import os
import queue
import socket
import threading
import time
from typing import BinaryIO, Callable, Protocol

from .errors import McpError

# the size of one transport read, which the line pump splits into replies
_READ_SIZE = 65536

# how long a named pipe may stay busy before a connect fails
_PIPE_CONNECT_TIMEOUT = 5.0

# the windows status for a pipe whose only instance still serves another client
_ERROR_PIPE_BUSY = 231

# the pause between two attempts to open a busy pipe
_PIPE_RETRY_INTERVAL = 0.05


class LineTransport(Protocol):

    # connect to the endpoint the host published
    def connect(self) -> None:
        ...

    # write one newline framed request
    def send_line(self, line: str) -> None:
        ...

    # read one newline framed reply within a deadline
    def read_line(self, timeout: float) -> str:
        ...

    # release the endpoint and any reader thread
    def close(self) -> None:
        ...


class _LinePump:

    def __init__(self, read: Callable[[int], bytes], errors: queue.Queue[BaseException]) -> None:
        # read pulls one chunk from the underlying endpoint
        self._read = read
        # errors carries a reader failure to the caller's next read
        self._errors = errors
        # stop tells the reader thread to finish
        self._stop = threading.Event()
        # lines holds complete replies the reader has decoded
        self._lines: queue.Queue[str] = queue.Queue()
        # thread is the reader that turns chunks into lines
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        # the reader runs beside the caller so a blocked read has a bounded wait
        self._thread = threading.Thread(target=self._run, daemon=True)
        # a daemon thread cannot keep the process alive after a failed launch
        self._thread.start()

    def stop(self) -> None:
        # signal the reader before closing the endpoint it reads
        self._stop.set()
        # join briefly so a close cannot race the last read
        if self._thread is not None:
            self._thread.join(timeout=1.0)

    def read_line(self, timeout: float) -> str:
        # surface a reader failure before waiting for another line
        try:
            error = self._errors.get_nowait()
        except queue.Empty:
            error = None
        # an error from the reader is reported as a transport failure
        if error is not None:
            raise McpError(str(error))
        # wait for the complete line that answers the request
        try:
            return self._lines.get(timeout=timeout)
        except queue.Empty as empty:
            # a reply that never arrives is a bounded failure, not a hang
            raise McpError(f"no reply within {timeout} seconds") from empty

    def _run(self) -> None:
        # bytes received but not yet terminated by a newline
        buffer = b""
        while not self._stop.is_set():
            # one read from the endpoint, which may return any number of bytes
            try:
                chunk = self._read(_READ_SIZE)
            except BaseException as error:
                # a read that failed because of stop is not a failure
                if not self._stop.is_set():
                    self._errors.put(error)
                return
            # an empty read means the host closed the conversation
            if not chunk:
                self._errors.put(McpError("the connection was closed by the server"))
                return
            # append the chunk to any partial line left from the last read
            buffer += chunk
            while b"\n" in buffer:
                # split at the first newline, which ends one json-rpc message
                line, buffer = buffer.split(b"\n", 1)
                # hand the decoded line to the caller's reader
                self._lines.put(line.decode("utf-8", errors="replace"))


class UnixSocketTransport:

    def __init__(self, endpoint: str) -> None:
        # endpoint is the socket path the host published in its descriptor
        self._endpoint = endpoint
        # socket is the connected stream held for the whole conversation
        self._socket: socket.socket | None = None
        # pump turns the stream's bytes into complete reply lines
        self._pump = _LinePump(self._read, self._errors)

    @property
    def _errors(self) -> queue.Queue[BaseException]:
        # errors carries reader failures to read_line
        if not hasattr(self, "_error_queue"):
            self._error_queue: queue.Queue[BaseException] = queue.Queue()
        return self._error_queue

    def _read(self, size: int) -> bytes:
        # delegate one chunk read to the connected socket
        if self._socket is None:
            return b""
        return self._socket.recv(size)

    def connect(self) -> None:
        # the host offers a unix domain socket on macos and linux
        try:
            self._socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self._socket.connect(self._endpoint)
        except OSError as error:
            raise McpError(f"connection to {self._endpoint} failed: {error}") from error
        # reading starts only after the connection was accepted
        self._pump.start()

    def send_line(self, line: str) -> None:
        # the newline completes one json-rpc request
        try:
            self._socket.sendall((line + "\n").encode("utf-8"))
        except OSError as error:
            raise McpError(f"the connection dropped before the request: {error}") from error

    def read_line(self, timeout: float) -> str:
        # take one reply from the reader's queue
        return self._pump.read_line(timeout)

    def close(self) -> None:
        # shutdown unblocks the reader before the descriptor is released
        if self._socket is not None:
            try:
                self._socket.shutdown(socket.SHUT_RDWR)
            except OSError:
                # an already disconnected peer needs no shutdown
                pass
            self._socket.close()
            self._socket = None
        # stop the reader after the descriptor it reads is gone
        self._pump.stop()


class NamedPipeTransport:

    def __init__(self, endpoint: str) -> None:
        # endpoint is the pipe name the host published in its descriptor
        self._endpoint = endpoint
        # pipe is the binary handle Python opens for a named pipe
        self._pipe: BinaryIO | None = None
        # pump turns the pipe's bytes into complete reply lines
        self._pump = _LinePump(self._read, self._errors)

    @property
    def _errors(self) -> queue.Queue[BaseException]:
        # errors carries reader failures to read_line
        if not hasattr(self, "_error_queue"):
            self._error_queue: queue.Queue[BaseException] = queue.Queue()
        return self._error_queue

    def _read(self, size: int) -> bytes:
        # delegate one chunk read to the open pipe
        if self._pipe is None:
            return b""
        return self._pipe.read(size)

    def connect(self) -> None:
        # a single instance pipe may still be busy with the previous client
        deadline = time.monotonic() + _PIPE_CONNECT_TIMEOUT
        while True:
            # a windows named pipe opens through the same builtin file interface
            try:
                self._pipe = open(self._endpoint, "r+b", buffering=0)
                break
            except OSError as error:
                # a busy pipe is retried until the host finishes its serve pass
                if getattr(error, "winerror", None) == _ERROR_PIPE_BUSY and time.monotonic() < deadline:
                    time.sleep(_PIPE_RETRY_INTERVAL)
                    continue
                # every other open failure is reported to the caller
                raise McpError(f"connection to {self._endpoint} failed: {error}") from error
        # reading starts only after the pipe accepted the client
        self._pump.start()

    def send_line(self, line: str) -> None:
        # the newline completes one json-rpc request
        try:
            self._pipe.write((line + "\n").encode("utf-8"))
        except OSError as error:
            raise McpError(f"the connection dropped before the request: {error}") from error

    def read_line(self, timeout: float) -> str:
        # take one reply from the reader's queue
        return self._pump.read_line(timeout)

    def close(self) -> None:
        # release the pipe first, which unblocks a reader waiting on it
        if self._pipe is not None:
            self._pipe.close()
            self._pipe = None
        # stop the reader after the handle it reads is gone
        self._pump.stop()


def default_transport(endpoint: str) -> LineTransport:
    # windows publishes a named pipe and the other platforms a unix socket
    if os.name == "nt":
        return NamedPipeTransport(endpoint)
    return UnixSocketTransport(endpoint)
