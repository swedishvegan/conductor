
from fsop import DEFAULT_COMMANDS, cmd_list, cmd_read, cmd_write_append, cmd_read_paginated, cmd_edit_page, cmd_query_modules, cmd_create_module
from validate_arguments import validate_arguments

def load_commands(): # loads a dictionary of all existing commands; both default and plugins (TODO)

    commands = DEFAULT_COMMANDS
    return commands

ALL_COMMANDS = load_commands()

def parser_validate(parsedactions):
    '''
    Helper function used by the parser in the client. This is written server-side because the client does not have complete information on all plugins that are installed and what their function signatures are.

    parsedactions is assumed to be a list of dicts
    '''

    def pverr(agent, line, name, reason):
        return { 
            "status": "err",
            "reason": f"Failed to parse `{agent}`: Invalid action `{name}` on line {line}: {reason}" 
        }

    error = None

    for action in parsedactions:

        agent = action["agent"]
        line = action["line"]
        name = action["name"]
        args = action["args"]
        isuser = action["isuser"]

        if name not in ALL_COMMANDS:
            return pverr(agent, line, name, "Command does not exist")

        validation = validate_arguments(args, ALL_COMMANDS[name])

        for arg in args.keys():
            if not validation[arg]["valid"]:
                return pverr(agent, line, name, f"Argument `{arg}` does not exist or did not match expected format")

        # if it gets this far, you may assume every argument is valid

        if len(validation.keys()) == 0: continue # agent is allowed to call actions with no arguments

        iscomplete = all( validation[arg]["exists"] for arg in validation.keys() ) # whether every possible argument, required or not, is specified

        if not isuser and iscomplete:
            return pverr(agent, line, name, "Agent action must have at least one unspecified argument; otherwise there is no point in making it an agent action")

    return { "status": "ok" }

def perform_action(action):

    return { "status": "err", "reason": "Not implemented yet" }

# main server loop; code below was mostly written by chatgpt

import asyncio, signal, os, struct, json

SOCKET_PATH = "/tmp/hll_socket.sock"
HLL_DIR = os.path.expanduser("~/.local/share/hll/")
PIDFILE = "/tmp/hll_server.pid"

# Clean up old socket if exists
if os.path.exists(SOCKET_PATH):
    os.remove(SOCKET_PATH)

# Helper: read exactly n bytes
async def recv_all(reader, n):
    data = b''
    while len(data) < n:
        chunk = await reader.read(n - len(data))
        if not chunk:
            raise ConnectionError("Connection closed while reading data")
        data += chunk
    return data

async def handle_client(reader, writer):
    try:
        raw_len = await recv_all(reader, 4)
        msg_len = struct.unpack('!I', raw_len)[0]
        raw_json = await recv_all(reader, msg_len)
        data = json.loads(raw_json.decode())
        print(f"\tReceived JSON: {data}")

        request_type = data["request"]
        data = data["data"]

        if request_type == "validate_actions":
            response = parser_validate(data)
        elif request_type == "perform_action":
            response = perform_action(data)
        else:
            response = { "status": "err", "reason": f"Unrecognized request type `{request_type}`" }

        if response["status"] == "ok":
            print("\t\tHandled request with no issues")
        else:
            print(f"\t\tFailed to handle request: {response['reason']}")

        encoded = json.dumps(response).encode()
        writer.write(struct.pack('!I', len(encoded)))
        writer.write(encoded)
        await writer.drain()

    except (ConnectionResetError, BrokenPipeError):
        print("Client disconnected before response could be sent.")
    except Exception as e:
        print(f"HLL Server Error: {e}")
    finally:
        try:
            writer.close()
            await writer.wait_closed()
        except BrokenPipeError:
            pass  # Ignore if the pipe was already closed

loop = asyncio.get_event_loop()
stop_event = asyncio.Event()

def _sigterm(*_):
    stop_event.set()

signal.signal(signal.SIGTERM, _sigterm)

async def main():
    # === Write the PID file ===
    with open(PIDFILE, "w") as f:
        f.write(str(os.getpid()))
        f.flush()
        os.fsync(f.fileno())
    print(f"Server PID written to {PIDFILE}")

    try:
        server = await asyncio.start_unix_server(handle_client, path=SOCKET_PATH)
        async with server:
            await stop_event.wait()        # run until SIGTERM
        await server.wait_closed()         # graceful shutdown
    finally:
        # === Always clean up the PID file on exit ===
        try:
            if os.path.exists(PIDFILE):
                os.remove(PIDFILE)
                print(f"Removed {PIDFILE}")
        except Exception as e:
            print(f"Failed to remove PID file: {e}")

asyncio.run(main())
