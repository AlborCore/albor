import logging
import os

# the logger namespace keeps protocol diagnostics under one name
_FRAMEWORK_LOGGER_NAME = "albor_automation"

# the debug handler installed once, so repeated configuration never doubles lines
_DEBUG_HANDLER: logging.StreamHandler | None = None


def logger() -> logging.Logger:
    # return the logger every harness module shares
    return logging.getLogger(_FRAMEWORK_LOGGER_NAME)


def configure_from_environment() -> None:
    # a caller that did not ask for protocol diagnostics keeps the default level
    if not os.environ.get("ALBOR_AUTOMATION_DEBUG"):
        return
    # the package logger carries the protocol lines
    package_logger = logger()
    # debug is the level that carries request and reply lines
    package_logger.setLevel(logging.DEBUG)
    # the module keeps the one handler it installs
    global _DEBUG_HANDLER
    # a handler is installed once, so repeated configuration never doubles output
    if _DEBUG_HANDLER is None:
        _DEBUG_HANDLER = logging.StreamHandler()
        package_logger.addHandler(_DEBUG_HANDLER)
