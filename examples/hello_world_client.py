"""
Example CoAP client for the hello_world server.

Requires:
    pip install aiocoap protobuf

hello_world_pb2.py was generated with:
    protoc --python_out=examples examples/hello_world.proto

Start the server first:
    ./build/examples/coap-hello-world

Then run:
    python examples/hello_world_client.py
"""

import asyncio
import aiocoap

import hello_world_pb2

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


async def main():
    ctx = await aiocoap.Context.create_client_context()

    hello_req = hello_world_pb2.HelloRequest(name="World")

    await get(ctx, "/hello")                                                  # 2.05 Content: "Hello, CoAP World!"
    await get(ctx, "/slow")                                                   # 2.05 Content after ~2 s delay
    await get(ctx, "/other")                                                  # 4.04 Not Found
    await post_pb(ctx, "/hello-world-pb", hello_req, hello_world_pb2.HelloResponse)  # 2.05 HelloResponse
    await post_pb(ctx, "/hello-lambda-pb", hello_req, hello_world_pb2.HelloResponse) # 2.05 HelloResponse

    await ctx.shutdown()


if __name__ == "__main__":
    asyncio.run(main())
