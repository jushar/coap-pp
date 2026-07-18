"""
Example CoAP client for the serde_nanopb server.

Requires:
    pip install aiocoap protobuf

serde_nanopb_pb2.py was generated with:
    protoc --python_out=examples/serde_nanopb examples/serde_nanopb/serde_nanopb.proto

Start the server first:
    ./build/examples/coap-serde-nanopb

Then run:
    python examples/serde_nanopb/serde_nanopb_client.py
"""

import asyncio
import aiocoap

import serde_nanopb_pb2

BASE_URI = "coap://127.0.0.1:5683"


async def get(ctx, path):
    req = aiocoap.Message(code=aiocoap.GET, uri=f"{BASE_URI}{path}")
    print(f"GET {path} ...", end=" ", flush=True)
    resp = await ctx.request(req).response
    body = resp.payload.decode(errors="replace") if resp.payload else ""
    print(f"{resp.code}  {body!r}")


async def post_pb(ctx, path, request_msg, response_type=None):
    req = aiocoap.Message(
        code=aiocoap.POST,
        uri=f"{BASE_URI}{path}",
        payload=request_msg.SerializeToString(),
    )
    print(f"POST {path} ...", end=" ", flush=True)
    resp = await ctx.request(req).response
    if response_type and resp.payload:
        msg = response_type()
        msg.ParseFromString(resp.payload)
        print(f"{resp.code}  {msg}")
    else:
        body = resp.payload.decode(errors="replace") if resp.payload else ""
        print(f"{resp.code}  {body!r}")


async def observe_counter_pb(ctx, notification_count=2):
    """Observe /counter-pb (RFC 7641): the registration response doubles as
    the initial state; the server then notifies every 5 s with a protobuf
    CounterValue payload when the counter increments."""

    def counter_of(message):
        value = serde_nanopb_pb2.CounterValue()
        value.ParseFromString(message.payload)
        return value.value

    req = aiocoap.Message(code=aiocoap.GET, uri=f"{BASE_URI}/counter-pb",
                          observe=0)
    request = ctx.request(req)

    resp = await request.response
    print(f"GET /counter-pb  {resp.code}  observe={resp.opt.observe}  "
          f"value={counter_of(resp)}")

    received = 0
    async for notification in request.observation:
        print(f"  notification  {notification.code}  "
              f"observe={notification.opt.observe}  "
              f"value={counter_of(notification)}")
        received += 1
        if received >= notification_count:
            request.observation.cancel()
            break


async def main():
    ctx = await aiocoap.Context.create_client_context()

    hello_req = serde_nanopb_pb2.HelloRequest(name="World")

    await get(ctx, "/hello")                                                  # 2.05 Content: "Hello, CoAP World!"
    await get(ctx, "/slow")                                                   # 2.05 Content after ~2 s delay
    await get(ctx, "/other")                                                  # 4.04 Not Found
    await post_pb(ctx, "/hello-world-pb", hello_req, serde_nanopb_pb2.HelloResponse)  # 2.05 HelloResponse
    await post_pb(ctx, "/hello-lambda-pb", hello_req, serde_nanopb_pb2.HelloResponse) # 2.05 HelloResponse

    print("observing /counter-pb (one notification every ~5 s) ...")
    await observe_counter_pb(ctx)                                             # 2.05 initial + notifications

    await ctx.shutdown()


if __name__ == "__main__":
    asyncio.run(main())
