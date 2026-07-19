"""
Example CoAP client for the blockwise server (RFC 7959).

aiocoap handles block-wise transfers transparently: it reassembles the
Block2 download from /image and fragments the large PUT payload to
/firmware into Block1 blocks, so this doubles as an interop test against
a real client stack.

Requires:
    pip install aiocoap

Start the server first:
    ./build/examples/coap-blockwise

Then run:
    python examples/blockwise/blockwise_client.py
"""

import asyncio

import aiocoap

BASE_URI = "coap://127.0.0.1:5683"

IMAGE_SIZE = 4000
FIRMWARE_SIZE = 5000
CONFIG_SIZE = 1500
CONFIG_LIMIT = 2048  # server-side reassembly buffer capacity


async def main():
    ctx = await aiocoap.Context.create_client_context()

    # Download: the server serves /image in Block2 blocks; aiocoap requests
    # block after block and hands us the reassembled 4000-byte body.
    req = aiocoap.Message(code=aiocoap.GET, uri=f"{BASE_URI}/image")
    resp = await ctx.request(req).response
    expected = bytes(ord("A") + (i % 26) for i in range(IMAGE_SIZE))
    assert resp.payload == expected, (
        f"unexpected /image body ({len(resp.payload)} bytes)")
    print(f"GET /image  {resp.code}  {len(resp.payload)} bytes "
          f"reassembled from Block2 blocks (etag={resp.opt.etag.hex()})")

    # Upload: the payload exceeds a single message, so aiocoap sends it as a
    # Block1 transfer; the server streams the blocks into its firmware buffer
    # and answers 2.31 Continue until the final block.
    firmware = bytes((i * 7) & 0xFF for i in range(FIRMWARE_SIZE))
    req = aiocoap.Message(code=aiocoap.PUT, uri=f"{BASE_URI}/firmware",
                          payload=firmware)
    resp = await ctx.request(req).response
    print(f"PUT /firmware  {resp.code}  "
          f"uploaded {len(firmware)} bytes via Block1")

    # Assembled upload: the server collects the /config blocks into a buffer
    # and only processes the body once it is complete.
    config = b'{"mode": "demo"}' * (CONFIG_SIZE // 16)
    req = aiocoap.Message(code=aiocoap.POST, uri=f"{BASE_URI}/config",
                          payload=config)
    resp = await ctx.request(req).response
    print(f"POST /config  {resp.code}  "
          f"uploaded {len(config)} bytes via Block1 (reassembled)")

    await ctx.shutdown()


if __name__ == "__main__":
    asyncio.run(main())
