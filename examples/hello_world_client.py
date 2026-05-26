"""
Example CoAP client for the hello_world server.

Requires:
    pip install aiocoap

Start the server first:
    ./build/examples/coap-hello-world

Then run:
    python examples/hello_world_client.py
"""

import asyncio
import aiocoap

BASE_URI = "coap://127.0.0.1:5683"


async def main():
    ctx = await aiocoap.Context.create_client_context()

    requests = [
        ("GET",  "/hello"),   # 2.05 Content: "Hello, CoAP World!"
        ("GET",  "/slow"),    # 2.05 Content after ~2 s delay
        ("POST", "/hello"),   # 4.05 Method Not Allowed
        ("GET",  "/other"),   # 4.04 Not Found
    ]

    for method, path in requests:
        code = aiocoap.GET if method == "GET" else aiocoap.POST
        req = aiocoap.Message(code=code, uri=f"{BASE_URI}{path}")
        print(f"{method} {path} ...", end=" ", flush=True)
        resp = await ctx.request(req).response
        payload = resp.payload.decode(errors="replace") if resp.payload else ""
        print(f"{resp.code}  {payload!r}")

    await ctx.shutdown()


if __name__ == "__main__":
    asyncio.run(main())
