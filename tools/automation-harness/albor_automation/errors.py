class AlborTestError(Exception):

    # the root of every error the harness raises
    pass


class ApplicationStartupError(AlborTestError):

    # the application never published a reachable automation endpoint
    pass


class McpError(AlborTestError):

    # a transport, handshake or tool call was refused
    pass


class LocatorError(AlborTestError):

    # a semantic id could not be resolved in the live tree
    pass


class AlborAssertionError(AssertionError, AlborTestError):

    # a polling assertion ran out of time
    pass
