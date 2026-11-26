import os
from pathlib import Path
from types import TracebackType

from .albor_application import AlborApplication

# the default root for failure artifacts
DEFAULT_ARTIFACTS_DIRECTORY = "artifacts"

# the environment override for the artifacts root
ARTIFACTS_ENVIRONMENT_VARIABLE = "ALBOR_TEST_ARTIFACTS"


class TestSession:

    def __init__(self, app: AlborApplication, name: str, artifacts_root: str | None = None) -> None:
        # app is the application instance this session owns
        self._app = app
        # name scopes the artifacts of this session
        self._name = name
        # artifacts_root falls back to the environment and then the default
        self._artifacts_root = artifacts_root or os.environ.get(ARTIFACTS_ENVIRONMENT_VARIABLE, DEFAULT_ARTIFACTS_DIRECTORY)

    def app(self) -> AlborApplication:
        # return the application this session owns
        return self._app

    def finish(self, failed: bool) -> None:
        # collect artifacts when the caller reports a failure
        if failed:
            self._app.collect_artifacts(str(Path(self._artifacts_root) / self._name))
        # close the application at the end of the session
        self._app.close()

    def __enter__(self) -> AlborApplication:
        # hand the application to a with block
        return self._app

    def __exit__(self, exc_type: type[BaseException] | None, exc_val: BaseException | None, exc_tb: TracebackType | None) -> bool:
        # collect artifacts when the block raised and close the application
        self.finish(exc_type is not None)
        # let any exception in the block propagate
        return False
