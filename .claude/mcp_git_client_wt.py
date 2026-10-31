import asyncio
import json
import sys
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

# Same as mcp_git_client.py but the allowed repository is passed as argv[1] (a worktree root),
# so task commits land in their own worktree while still going through the Git MCP server.
async def main():
    repo = sys.argv[1]
    parameters = StdioServerParameters(
        command=sys.executable,
        args=["-m", "mcp_server_git", "--repository", repo],
    )
    async with stdio_client(parameters) as (reader, writer):
        async with ClientSession(reader, writer) as session:
            await session.initialize()
            if sys.argv[2] == "list":
                result = await session.list_tools()
                print(json.dumps([{"name": t.name, "inputSchema": t.inputSchema}
                                  for t in result.tools], ensure_ascii=True))
            else:
                with open(sys.argv[3], encoding="utf-8-sig") as handle:
                    arguments = json.load(handle)
                result = await session.call_tool(sys.argv[2], arguments)
                print(result.model_dump_json())
                if getattr(result, "is_error", getattr(result, "isError", False)):
                    raise RuntimeError("Git MCP call failed")

asyncio.run(main())
