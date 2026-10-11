"""Explicit local desktop endpoint; never resolves or configures USB hardware."""
import re
import socket
from dataclasses import dataclass


@dataclass(frozen=True)
class LoopbackEndpoint:
    port: int
    token: str

    def __post_init__(self):
        if not 1 <= self.port <= 65535 or not re.fullmatch('[0-9a-f]{64}', self.token):
            raise ValueError('Invalid desktop loopback endpoint')

    def connect(self):
        stream = socket.create_connection(('127.0.0.1', self.port), timeout=5)
        try:
            stream.sendall(('XTGV/1 ' + self.token + '\n').encode('ascii'))
            stream.setblocking(False)
            return stream
        except BaseException:
            stream.close()
            raise
