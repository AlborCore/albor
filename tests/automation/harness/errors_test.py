import unittest

from albor_automation.errors import AlborAssertionError, AlborTestError, ApplicationStartupError, LocatorError, McpError


class ErrorHierarchyChecks(unittest.TestCase):

    def test_every_error_shares_the_root(self) -> None:
        # act
        for error in (ApplicationStartupError, McpError, LocatorError, AlborAssertionError):
            self.assertTrue(issubclass(error, AlborTestError))

    def test_assertion_error_is_an_assertion(self) -> None:
        # assert
        self.assertTrue(issubclass(AlborAssertionError, AssertionError))


if __name__ == "__main__":
    unittest.main()
