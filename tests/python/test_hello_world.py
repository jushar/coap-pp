"""
Integration tests for the coap-hello-world server.

Requires:
    pip install -r tests/python/requirements.txt

Run from the repo root after building:
    pytest tests/python/test_hello_world.py -v

The SERVER_BIN path can be overridden via the COAP_SERVER_BIN environment variable:
    COAP_SERVER_BIN=./build/examples/coap-hello-world pytest tests/python/ -v
"""

import asyncio
import os
import subprocess
import time

import aiocoap
import pytest

SERVER_BIN = os.environ.get(
    "COAP_SERVER_BIN",
    os.path.join(os.path.dirname(__file__), "..", "..", "build", "examples", "coap-hello-world"),
)
BASE_URI = "coap://127.0.0.1:5683"


# ── Fixtures ──────────────────────────────────────────────────────────────────

@pytest.fixture(scope="module")
def server():
    """Start the hello-world binary once for the whole module, stop after."""
    proc = subprocess.Popen(
        [SERVER_BIN],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(0.4)  # let the socket bind and receive thread start
    yield proc
    proc.terminate()
    proc.wait(timeout=5)


@pytest.fixture(scope="module")
def event_loop():
    loop = asyncio.new_event_loop()
    yield loop
    loop.close()


@pytest.fixture(scope="module")
def coap_ctx(event_loop):
    ctx = event_loop.run_until_complete(aiocoap.Context.create_client_context())
    yield ctx
    event_loop.run_until_complete(ctx.shutdown())


# ── Helpers ───────────────────────────────────────────────────────────────────

def coap_get(ctx, event_loop, path):
    req = aiocoap.Message(code=aiocoap.GET, uri=f"{BASE_URI}{path}")
    return event_loop.run_until_complete(ctx.request(req).response)


def coap_post(ctx, event_loop, path, payload=b""):
    req = aiocoap.Message(
        code=aiocoap.POST,
        uri=f"{BASE_URI}{path}",
        payload=payload,
    )
    return event_loop.run_until_complete(ctx.request(req).response)


# ── Tests ─────────────────────────────────────────────────────────────────────

def test_get_hello_returns_success(server, coap_ctx, event_loop):
    """GET /hello should return 2.05 Content with the greeting text."""
    resp = coap_get(coap_ctx, event_loop, "/hello")
    assert resp.code.is_successful(), f"Expected 2.xx, got {resp.code}"
    assert b"Hello" in resp.payload, f"Unexpected payload: {resp.payload!r}"
    print(f"\n  {resp.code}  {resp.payload.decode()}")


def test_get_hello_content_format(server, coap_ctx, event_loop):
    """GET /hello should include Content-Format: text/plain (0)."""
    resp = coap_get(coap_ctx, event_loop, "/hello")
    cf = resp.opt.content_format
    assert cf == 0, f"Expected Content-Format 0 (text/plain), got {cf}"


def test_get_missing_returns_404(server, coap_ctx, event_loop):
    """GET on an unregistered path should return 4.04 Not Found."""
    resp = coap_get(coap_ctx, event_loop, "/missing")
    assert resp.code == aiocoap.Code.NOT_FOUND, f"Expected 4.04, got {resp.code}"
    print(f"\n  {resp.code}")


def test_post_hello_returns_405(server, coap_ctx, event_loop):
    """POST /hello should return 4.05 Method Not Allowed."""
    resp = coap_post(coap_ctx, event_loop, "/hello", b"data")
    assert resp.code == aiocoap.Code.METHOD_NOT_ALLOWED, f"Expected 4.05, got {resp.code}"
    print(f"\n  {resp.code}")


def test_multiple_sequential_requests(server, coap_ctx, event_loop):
    """Server should handle several back-to-back requests without errors."""
    for i in range(5):
        resp = coap_get(coap_ctx, event_loop, "/hello")
        assert resp.code.is_successful(), f"Request {i} failed: {resp.code}"
