"""Failures of the aquaOne read-only HTTP API."""


class AquaOneError(Exception):
    """Base API failure."""


class AquaOneConnectionError(AquaOneError):
    """The device could not be reached."""


class AquaOneTimeoutError(AquaOneError):
    """The device did not respond within the client timeout."""


class AquaOneInvalidResponseError(AquaOneError):
    """The response is not a valid supported device snapshot."""


class AquaOneUnsupportedProtocolError(AquaOneError):
    """The device uses an unsupported API major version."""


class AquaOneAuthenticationError(AquaOneError):
    """The device rejected access."""


class AquaOneDeviceRejectedError(AquaOneError):
    """The device rejected a read request."""
