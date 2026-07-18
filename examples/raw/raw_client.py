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


async def observe_counter(ctx, notification_count=2):
    """Observe /counter (RFC 7641): the registration response doubles as the
    initial state; the server then notifies every 5 s when the counter
    increments."""
    req = aiocoap.Message(code=aiocoap.GET, uri=f"{BASE_URI}/counter",
                          observe=0)
    request = ctx.request(req)

    resp = await request.response
    print(f"GET /counter  {resp.code}  observe={resp.opt.observe}  "
          f"{resp.payload.decode()!r}")

    received = 0
    async for notification in request.observation:
        print(f"  notification  {notification.code}  "
              f"observe={notification.opt.observe}  "
              f"{notification.payload.decode()!r}")
        received += 1
        if received >= notification_count:
            request.observation.cancel()
            break


async def main():
    ctx = await aiocoap.Context.create_client_context()

    req = aiocoap.Message(code=aiocoap.GET, uri=f"{BASE_URI}/hello")
    resp = await ctx.request(req).response
    print(f"GET /hello  {resp.code}  {resp.payload.decode()!r}")

    req = aiocoap.Message(code=aiocoap.POST, uri=f"{BASE_URI}/echo",
                          payload=b"ping")
    resp = await ctx.request(req).response
    print(f"POST /echo  {resp.code}  {resp.payload.decode()!r}")

    print("observing /counter (one notification every ~5 s) ...")
    await observe_counter(ctx)

    await ctx.shutdown()


if __name__ == "__main__":
    asyncio.run(main())
