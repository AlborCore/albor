import unittest

from .albor_application import AlborApplication
from .log import configure_from_environment
from .session import TestSession

# an optional adapter for suites that prefer setUp based sessions
class AlborTestCase(unittest.TestCase):

    def setUp(self) -> None:
        # enable protocol diagnostics when the environment requests them
        configure_from_environment()
        # each subclass starts its own session in setUp
        self._session: TestSession | None = None

    def tearDown(self) -> None:
        # finish the session, collecting artifacts on a failed test
        if self._session is not None:
            self._session.finish(self._test_failed())
        # clear the session reference after the test
        self._session = None

    def start_session(self, app: AlborApplication) -> None:
        # expose the application to the test body
        self._app = app
        # wrap the application in a session named by the test id
        self._session = TestSession(app, self.id())

    def _test_failed(self) -> bool:
        # read the runner outcome recorded for this test
        outcome = getattr(self, "_outcome", None)
        if outcome is None:
            return False
        result = getattr(outcome, "result", None)
        if result is None:
            return False
        return any(entry[0] is self for entry in list(result.failures) + list(result.errors))
