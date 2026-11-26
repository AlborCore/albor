import json
import os
import re
import socket
import subprocess
import tempfile
import time
from pathlib import Path
from types import TracebackType
from typing import Any, BinaryIO, Callable

from .albor_client import AlborClient
from .errors import ApplicationStartupError
from .locator import Locator
from .log import logger
from .mcp_client import McpClient
from .waiting import DEFAULT_POLL_INTERVAL, DEFAULT_WAIT_TIMEOUT, reports_test_frames, wait_for

# how long a launch may take before the automation endpoint is reachable
DEFAULT_STARTUP_TIMEOUT = 30.0

# how long a graceful stop may take before the process is killed
DEFAULT_TERMINATE_TIMEOUT = 5.0

# the environment override for the executable under test
DEFAULT_EXECUTABLE_ENVIRONMENT_VARIABLE = "ALBOR_TEST_APPLICATION"

# the line the host prints once the descriptor is published
DESCRIPTOR_PATTERN = re.compile(r"^albor automation: serving (?P<descriptor>.+)$")

# the logger that reports a launch attempt
_LOGGER = logger()


class AlborApplication:

    def __init__(self, process: subprocess.Popen, client: AlborClient, descriptor_path: Path, stop_path: Path, stdout_file: BinaryIO | None, stderr_file: BinaryIO | None, config_dir: tempfile.TemporaryDirectory | None) -> None:
        # process owns the application under test
        self._process = process
        # client is the authenticated facade over the automation layer
        self._client = client
        # descriptor_path is the host.json the application published
        self._descriptor_path = descriptor_path
        # stop_path tells the fixture app to close its window and exit
        self._stop_path = stop_path
        # stdout_file captures the child standard output
        self._stdout_file = stdout_file
        # stderr_file captures the child standard error
        self._stderr_file = stderr_file
        # config_dir isolates the child's persistent configuration
        self._config_dir = config_dir

    def client(self) -> AlborClient:
        # return the facade for protocol level conformance assertions
        return self._client

    def descriptor_path(self) -> Path:
        # return the descriptor the host published
        return self._descriptor_path

    def is_running(self) -> bool:
        # report whether the application process is still alive
        return self._process.poll() is None

    def get_by_id(self, node_id: str) -> Locator:
        # create a lazy locator for an author assigned id
        return Locator(self, node_id)

    @reports_test_frames
    def wait_for_condition(self, condition: Callable[[], bool], *, timeout: float = DEFAULT_WAIT_TIMEOUT, poll_interval: float = DEFAULT_POLL_INTERVAL, message: str) -> None:
        # poll until a caller supplied condition holds
        wait_for(condition, timeout=timeout, poll_interval=poll_interval, timeout_message=message, observe=self._tree_text)

    def screenshot(self, path: str) -> None:
        # capture the presented frame into a file
        Path(path).write_bytes(self._client.screenshot())

    def tree_text(self) -> str:
        # read the raw tree dump for diagnostics
        return self._tree_text()

    def stdout_text(self) -> str:
        # report no output when the stream was not captured
        if self._stdout_file is None:
            return ""
        # rewind and decode the captured output
        self._stdout_file.seek(0)
        return self._stdout_file.read().decode(errors="replace")

    def stderr_text(self) -> str:
        # report no output when the stream was not captured
        if self._stderr_file is None:
            return ""
        # rewind and decode the captured errors
        self._stderr_file.seek(0)
        return self._stderr_file.read().decode(errors="replace")

    def collect_artifacts(self, directory: str) -> str:
        # create the artifact directory for a failed test
        path = Path(directory)
        path.mkdir(parents=True, exist_ok=True)
        # capture the last frame when one can be read
        try:
            self.screenshot(str(path / "screenshot.png"))
        except Exception:
            # a failed capture must not mask the test failure
            pass
        # capture the tree dump when the layer still answers
        try:
            (path / "tree.txt").write_text(self.tree_text())
        except Exception:
            # a failed dump must not mask the test failure
            pass
        # capture the descriptor for connection diagnostics
        try:
            (path / "descriptor.json").write_text(self._descriptor_path.read_text())
        except Exception:
            # a removed descriptor must not mask the test failure
            pass
        # capture both child output streams for the failure report
        (path / "stdout.txt").write_text(self.stdout_text())
        (path / "stderr.txt").write_text(self.stderr_text())
        # report where the artifacts were written
        _LOGGER.info("collected failure artifacts in %s", path)
        return str(path)

    def close(self) -> None:
        # ask a live application to stop through its stop file
        if self._process.poll() is None:
            self._stop_path.write_text("stop\n")
            try:
                self._process.wait(timeout=DEFAULT_TERMINATE_TIMEOUT)
            except subprocess.TimeoutExpired:
                # a process that ignores the stop file is terminated
                self._process.terminate()
                try:
                    self._process.wait(timeout=DEFAULT_TERMINATE_TIMEOUT)
                except subprocess.TimeoutExpired:
                    # a process that ignores termination is killed
                    self._process.kill()
                    self._process.wait(timeout=DEFAULT_TERMINATE_TIMEOUT)
        # release the transport even when the process died on its own
        self._client._mcp.close()
        # release the captured streams and the isolated configuration
        self._close_output_files()
        self._close_config_dir()

    def __enter__(self) -> "AlborApplication":
        # hand the application to a with block
        return self

    def __exit__(self, exc_type: type[BaseException] | None, exc_val: BaseException | None, exc_tb: TracebackType | None) -> bool:
        # close the application when the block ends
        self.close()
        # let any exception in the block propagate
        return False

    def _tree_text(self) -> str:
        # observe the live tree for wait failure messages
        return self._client.call_tool("semantic.tree")

    def _close_output_files(self) -> None:
        # release both captured streams when present
        for file in (self._stdout_file, self._stderr_file):
            if file is not None:
                file.close()
        # clear the references so a second close is harmless
        self._stdout_file = None
        self._stderr_file = None

    def _close_config_dir(self) -> None:
        # nothing to clean when no isolated directory was created
        if self._config_dir is None:
            return
        # remove the per launch configuration directory
        self._config_dir.cleanup()
        self._config_dir = None


def launch(executable_path: str | None = None, *, startup_timeout: float = DEFAULT_STARTUP_TIMEOUT, env: dict[str, str] | None = None, args: list[str] | None = None, config_dir: bool = True, connect: bool = True) -> AlborApplication:
    # resolve the executable before starting any process
    path = executable_path or resolve_executable()
    # one attempt owns the whole launch lifecycle
    return _launch_attempt(path, startup_timeout, env or {}, args or [], config_dir, connect)


def resolve_executable() -> str:
    # the caller must name the executable explicitly or through the environment
    override = os.environ.get(DEFAULT_EXECUTABLE_ENVIRONMENT_VARIABLE)
    if override:
        return override
    # an implicit build path would assume a layout the consumer does not have
    raise ApplicationStartupError(f"no application executable: pass executable_path to launch() or set {DEFAULT_EXECUTABLE_ENVIRONMENT_VARIABLE}")


def _launch_attempt(executable_path: str, startup_timeout: float, env_overrides: dict[str, str], command_arguments: list[str], config_dir: bool, connect: bool) -> AlborApplication:
    # prepare a temporary directory for the stop file and optional configuration
    scratch = tempfile.TemporaryDirectory(prefix="albor-automation-")
    stop_path = Path(scratch.name) / "stop"
    child_env = _child_environment(env_overrides)
    if config_dir and "XDG_CONFIG_HOME" not in child_env:
        child_env["XDG_CONFIG_HOME"] = str(Path(scratch.name) / "config")
    # spool both child streams so a failed launch can report them
    stdout_file = tempfile.TemporaryFile()
    stderr_file = tempfile.TemporaryFile()
    process: subprocess.Popen | None = None
    try:
        # start the fixture app with the stop file as its first argument
        process = subprocess.Popen([executable_path, str(stop_path)] + command_arguments, env=child_env, stdout=stdout_file, stderr=stderr_file)
        # wait until the host publishes and binds its endpoint
        descriptor_path = _wait_until_ready(process, stdout_file, stderr_file, startup_timeout)
        # read the descriptor the host published for the endpoint paths
        descriptor = json.loads(descriptor_path.read_text())
        # read the token file the descriptor names
        token = Path(descriptor["token"]).read_text().strip()
        # build the transport over the published socket or pipe
        mcp = McpClient(descriptor["socket"], token if connect else None)
        # the handshake proves the token and starts the conversation
        if connect:
            mcp.connect()
    except Exception:
        # every failure releases the process and the temporary resources
        _stop_process(process, stop_path)
        stdout_file.close()
        stderr_file.close()
        scratch.cleanup()
        raise
    # the application is ready once the handshake succeeded
    return AlborApplication(process, AlborClient(mcp), descriptor_path, stop_path, stdout_file, stderr_file, scratch)


def _wait_until_ready(process: subprocess.Popen, stdout_file: BinaryIO, stderr_file: BinaryIO, startup_timeout: float) -> Path:
    # bound the wait by one deadline for discovery and reachability
    deadline = time.monotonic() + startup_timeout
    descriptor_path: Path | None = None
    while time.monotonic() < deadline:
        # a process that exited before serving cannot answer anything
        if process.poll() is not None:
            stdout_file.seek(0)
            output = stdout_file.read().decode(errors="replace")
            stderr_file.seek(0)
            errors = stderr_file.read().decode(errors="replace")
            raise ApplicationStartupError(f"application exited with code {process.returncode} before the automation host served\noutput:\n{output}\nstandard error:\n{errors}")
        # the host reports its descriptor on either output stream
        output = _read_stream(stdout_file)
        errors = _read_stream(stderr_file)
        descriptor_path = _descriptor_in(output) or _descriptor_in(errors)
        # a directory scan covers a host whose report line was lost
        if descriptor_path is None:
            descriptor_path = _descriptor_for_pid(process.pid)
        if descriptor_path is not None:
            break
        # the host needs a frame to bind its endpoint
        time.sleep(0.1)
    # a host that never published a descriptor is a startup failure
    if descriptor_path is None:
        output = _read_stream(stdout_file)
        errors = _read_stream(stderr_file)
        raise ApplicationStartupError(f"automation host did not serve a descriptor within {startup_timeout}s\noutput:\n{output}\nstandard error:\n{errors}")
    # verify that the descriptor belongs to this process and speaks mcp
    descriptor = json.loads(descriptor_path.read_text())
    if descriptor.get("pid") != process.pid or descriptor.get("protocol") != "mcp":
        raise ApplicationStartupError(f"descriptor {descriptor_path} does not describe this application")
    # the socket or pipe only answers after the host finished binding
    _wait_until_endpoint(descriptor_path, deadline)
    return descriptor_path


def _read_stream(file: BinaryIO) -> str:
    # rewind and decode one captured stream
    file.seek(0)
    return file.read().decode(errors="replace")


def _descriptor_in(output: str) -> Path | None:
    # find the descriptor path in a serving line
    for line in output.splitlines():
        matched = DESCRIPTOR_PATTERN.match(line)
        if matched is not None:
            return Path(matched.group("descriptor"))
    return None


def _descriptor_for_pid(pid: int) -> Path | None:
    # scan the platform discovery bases for one process directory
    for directory in _discovery_directories(pid):
        descriptor = directory / "host.json"
        if descriptor.is_file():
            return descriptor
    return None


def _discovery_directories(pid: int) -> list[Path]:
    # build every directory the host may have used for this pid
    directories: list[Path] = []
    if os.name == "nt":
        local = os.environ.get("LOCALAPPDATA")
        if local:
            directories.append(Path(local) / "albor-mcp" / str(pid))
        return directories
    for key in ("XDG_RUNTIME_DIR", "TMPDIR"):
        value = os.environ.get(key)
        if value:
            directories.append(Path(value) / "albor-mcp" / str(pid))
    directories.append(Path("/tmp") / "albor-mcp" / str(pid))
    directories.append(Path("/tmp") / f"albor-{os.getuid()}" / str(pid))
    return directories


def _wait_until_endpoint(descriptor_path: Path, deadline: float) -> None:
    # the descriptor is written after the single windows pipe instance was created
    if os.name == "nt":
        return
    # probe the published endpoint until it accepts a client
    while time.monotonic() < deadline:
        descriptor = json.loads(descriptor_path.read_text())
        endpoint = descriptor["socket"]
        if _probe_endpoint(endpoint):
            return
        time.sleep(0.1)
    raise ApplicationStartupError(f"the automation endpoint {descriptor_path} did not become reachable")


def _probe_endpoint(endpoint: str) -> bool:
    # open the local endpoint once, without sending a request
    if os.name == "nt":
        try:
            handle = open(endpoint, "r+b", buffering=0)
            handle.close()
            return True
        except OSError:
            return False
    probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        probe.connect(endpoint)
        return True
    except OSError:
        return False
    finally:
        probe.close()


def _stop_process(process: subprocess.Popen | None, stop_path: Path) -> None:
    # ask a live process to stop and kill it when that fails
    if process is None or process.poll() is not None:
        return
    stop_path.write_text("stop\n")
    try:
        process.wait(timeout=DEFAULT_TERMINATE_TIMEOUT)
    except subprocess.TimeoutExpired:
        process.terminate()
        try:
            process.wait(timeout=DEFAULT_TERMINATE_TIMEOUT)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=DEFAULT_TERMINATE_TIMEOUT)


def _child_environment(overrides: dict[str, str]) -> dict[str, str]:
    # start from this process environment and apply caller overrides
    env = os.environ.copy()
    env.update(overrides)
    return env
