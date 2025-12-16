"""
MessageQ - High-performance inter-process communication library

This module provides Python bindings for the msgq IPC library,
enabling efficient message passing between processes.

Usage:
    from msgq import sub_sock, pub_sock, Poller
    
    # Create publisher
    pub = pub_sock("ipc:///tmp/msgq_example")
    pub.send(b"Hello, World!")
    
    # Create subscriber
    sub = sub_sock("ipc:///tmp/msgq_example")
    msg = sub.receive()
    print(msg)
"""

# Must be built with scons
from msgq.ipc_pyx import (
    Context,
    Poller,
    SubSocket,
    PubSocket,
    SocketEventHandle,
    toggle_fake_events,
    set_fake_prefix,
    get_fake_prefix,
    delete_fake_prefix,
    wait_for_one_event,
)
from msgq.ipc_pyx import MultiplePublishersError, IpcError

from typing import Optional, List

# Version information
__version__ = "1.0.0"
__author__ = "Comma AI"
__license__ = "MIT"

# Export public API
__all__ = [
    "Context",
    "Poller",
    "SubSocket",
    "PubSocket",
    "SocketEventHandle",
    "toggle_fake_events",
    "set_fake_prefix",
    "get_fake_prefix",
    "delete_fake_prefix",
    "wait_for_one_event",
    "MultiplePublishersError",
    "IpcError",
    "NO_TRAVERSAL_LIMIT",
    "context",
    "fake_event_handle",
    "pub_sock",
    "sub_sock",
    "drain_sock_raw",
]

# Verify imports
assert MultiplePublishersError
assert IpcError
assert toggle_fake_events
assert set_fake_prefix
assert get_fake_prefix
assert delete_fake_prefix
assert wait_for_one_event

# Constants
NO_TRAVERSAL_LIMIT = 2**64 - 1

# Global context instance
context = Context()


def fake_event_handle(
    endpoint: str,
    identifier: Optional[str] = None,
    override: bool = True,
    enable: bool = False
) -> SocketEventHandle:
    """
    Create a fake event handle for testing.
    
    Args:
        endpoint: The endpoint identifier
        identifier: The fake event identifier (defaults to current prefix)
        override: Whether to override the prefix
        enable: Whether to enable the handle immediately
        
    Returns:
        SocketEventHandle: The created event handle
    """
    identifier = identifier or get_fake_prefix()
    handle = SocketEventHandle(endpoint, identifier, override)
    if override:
        handle.enabled = enable
    
    return handle


def pub_sock(endpoint: str) -> PubSocket:
    """
    Create and connect a publisher socket.
    
    Args:
        endpoint: The endpoint to publish to (e.g., "ipc:///tmp/msgq")
        
    Returns:
        PubSocket: Connected publisher socket
        
    Example:
        pub = pub_sock("ipc:///tmp/msgq_example")
        pub.send(b"Hello")
    """
    sock = PubSocket()
    sock.connect(context, endpoint)
    return sock


def sub_sock(
    endpoint: str,
    poller: Optional[Poller] = None,
    addr: str = "127.0.0.1",
    conflate: bool = False,
    timeout: Optional[int] = None
) -> SubSocket:
    """
    Create and connect a subscriber socket.
    
    Args:
        endpoint: The endpoint to subscribe to (e.g., "ipc:///tmp/msgq")
        poller: Optional poller to register this socket with
        addr: The address to subscribe to (default: "127.0.0.1")
        conflate: Whether to conflate messages (only keep latest)
        timeout: Optional receive timeout in milliseconds
        
    Returns:
        SubSocket: Connected subscriber socket
        
    Example:
        sub = sub_sock("ipc:///tmp/msgq_example")
        msg = sub.receive()
        print(msg)
    """
    sock = SubSocket()
    sock.connect(context, endpoint, addr.encode('utf8'), conflate)
    
    if timeout is not None:
        sock.setTimeout(timeout)
    
    if poller is not None:
        poller.registerSocket(sock)
    
    return sock


def drain_sock_raw(sock: SubSocket, wait_for_one: bool = False) -> List[bytes]:
    """
    Receive all messages currently available on the queue.
    
    Args:
        sock: The subscriber socket to drain
        wait_for_one: If True, wait for at least one message
        
    Returns:
        List[bytes]: All received messages
        
    Example:
        messages = drain_sock_raw(sub)
        for msg in messages:
            print(msg)
    """
    ret: List[bytes] = []
    
    while True:
        if wait_for_one and len(ret) == 0:
            # Block until we get the first message
            dat = sock.receive()
        else:
            # Non-blocking receive for remaining messages
            dat = sock.receive(non_blocking=True)
        
        if dat is None:
            break
        
        ret.append(dat)
    
    return ret
