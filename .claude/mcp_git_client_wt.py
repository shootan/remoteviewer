import asyncio
import json
import sys
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

async def main():
    parameters = StdioServerParameters(
        command=sys.executable,
        args=["-m", "mcp_server_git", "--repository", "C:/work/gnlink-wt/feat-keepalive-multi"],
    )
    async with stdio_client(parameters) as (reader, writer):
        async with ClientSession(reader, writer) as session:
            await session.initialize()
            if sys.argv[1] == "list":
                result = await session.list_tools()
                print(json.dumps([{"name": t.name, "inputSchema": t.inputSchema}
                                  for t in result.tools], ensure_ascii=True))
            else:
                with open(sys.argv[2], encoding="utf-8-sig") as handle:
                    arguments = json.load(handle)
                result = await session.call_tool(sys.argv[1], arguments)
                print(result.model_dump_json())
                if result.isError:
                    raise RuntimeError("Git MCP call failed")

asyncio.run(main())
