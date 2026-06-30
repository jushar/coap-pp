"""
Example CoAP client for the serde_json server.

Requires:
    pip install aiocoap

Start the server first:
    ./build/examples/coap-serde-json

Then run:
    python examples/serde_json/serde_json_client.py
"""

import asyncio
import json

import aiocoap

BASE_URI = "coap://127.0.0.1:5683"

async def post_json(ctx, path, payload: dict, *, label=""):
    req = aiocoap.Message(
        code=aiocoap.POST,
        uri=f"{BASE_URI}{path}",
        payload=json.dumps(payload).encode(),
    )
    print(f"POST {path} {label}...", end=" ", flush=True)
    resp = await ctx.request(req).response
    body = resp.payload.decode(errors="replace") if resp.payload else ""
    try:
        parsed = json.loads(body)
        print(f"{resp.code}  {parsed}")
    except json.JSONDecodeError:
        print(f"{resp.code}  {body!r}")


async def main():
    ctx = await aiocoap.Context.create_client_context()

    await post_json(ctx, "/greet", {"name": "World"})               # 2.05 {"greeting": "Hello, World!"}
    
    await ctx.shutdown()


if __name__ == "__main__":
    asyncio.run(main())
