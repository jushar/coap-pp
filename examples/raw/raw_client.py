"""
Example CoAP client for the raw server.

Requires:
    pip install aiocoap

Start the server first:
    ./build/examples/coap-raw

Then run:
    python examples/raw/raw_client.py
"""

import asyncio

import aiocoap

BASE_URI = "coap://127.0.0.1:5683"


async def main():
    ctx = await aiocoap.Context.create_client_context()

    req = aiocoap.Message(code=aiocoap.GET, uri=f"{BASE_URI}/hello")
    resp = await ctx.request(req).response
    print(f"GET /hello  {resp.code}  {resp.payload.decode()!r}")

    req = aiocoap.Message(code=aiocoap.POST, uri=f"{BASE_URI}/echo",
                          payload=b"ping")
    resp = await ctx.request(req).response
    print(f"POST /echo  {resp.code}  {resp.payload.decode()!r}")

    await ctx.shutdown()


if __name__ == "__main__":
    asyncio.run(main())
