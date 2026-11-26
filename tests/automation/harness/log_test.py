import logging
import os
import unittest
from unittest import mock

from albor_automation import log as log_module
from albor_automation.log import configure_from_environment, logger


class LoggingChecks(unittest.TestCase):

    def setUp(self) -> None:
        self._previous = logger().level
        self.addCleanup(logger().setLevel, self._previous)

    def test_logger_uses_the_package_namespace(self) -> None:
        # assert
        self.assertEqual(logger().name, "albor_automation")

    def test_configuration_enables_debug_when_requested(self) -> None:
        # arrange
        with mock.patch.dict(os.environ, {"ALBOR_AUTOMATION_DEBUG": "1"}):
            # act
            configure_from_environment()
        # assert
        self.assertEqual(logger().level, logging.DEBUG)

    def test_configuration_installs_one_debug_handler(self) -> None:
        # arrange
        with mock.patch.dict(os.environ, {"ALBOR_AUTOMATION_DEBUG": "1"}):
            configure_from_environment()
            configure_from_environment()
        # assert: a second configuration does not double the protocol lines
        self.assertEqual(len([handler for handler in logger().handlers if handler is log_module._DEBUG_HANDLER]), 1)

    def test_configuration_leaves_the_level_alone_by_default(self) -> None:
        # arrange
        logger().setLevel(logging.WARNING)
        # act
        with mock.patch.dict(os.environ, {}, clear=True):
            configure_from_environment()
        # assert
        self.assertEqual(logger().level, logging.WARNING)


if __name__ == "__main__":
    unittest.main()
