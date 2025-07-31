
from fsop import DEFAULT_COMMANDS, DEFAULT_ACTIONS
from validate_arguments import validate_arguments

def load_commands(): # loads a dictionary of all existing commands; both default and plugins (TODO)

    commands = DEFAULT_COMMANDS
    return commands

ALL_COMMANDS = load_commands()
ALL_LEGAL_COMMANDS = { k: ALL_COMMANDS[k] for k in ALL_COMMANDS.keys() if k != "answer" }

def get_commands(): # returns a dictionary of all installed HLL commands
    return { "status": "ok", "data": ALL_COMMANDS }

def convert_fsop_output(items):

    return [
        {
            "parts": [
                {
                    "text": text
                }
            ],
            "role": "user"
        } for text in items
    ]

def run_action(content, partidx, proot, module, dgraph, expecting):
    
    part = content["parts"][partidx]["functionCall"]
    actname = part["name"]
    res = part["args"]
    newctx = [content]        

    try:

        if actname not in expecting:
            raise RuntimeError(f"Agent was suppposed to call one of the following functions: `{'`, `'.join(expecting)}` in this reply, but instead attempted to call `{actname}`.")

        if actname == "answer":
            return True, newctx, DEFAULT_ACTIONS["answer"](None, res, None, None)

        elif actname in DEFAULT_COMMANDS:
            newctx.extend(
                convert_fsop_output(DEFAULT_ACTIONS[actname](proot, res, module, dgraph))
            )
            return True, newctx, None

    except Exception as e:
        newctx.extend(
            convert_fsop_output([f"Error: {str(e)}"])
        )
        return False, newctx, None

    raise RuntimeError("Custom actions not yet implemented.")


def get_arg(d, a):

    try: return d[a]
    except: raise RuntimeError(f"Required argument `{a}` is missing from request")

def agent_messed_up(reason):
    return {
        "status": "ok",
        "data": {
            "new_context": convert_fsop_output(["Error: " + reason]),
            "agent_error": True
        }
    }

def handle_agent(data):

    try: data = json.loads(data) # data is passed from client still in string form to prevent a needless conversion to/from JSON
    except: return { "status": "err", "reason": "Failed to parse reply." }

    try:

        rtype = get_arg(data, "response_type")
        proot = get_arg(data, "project_root")
        module = get_arg(data, "module")
        dgraph = get_arg(data, "dependency_graph")
        expecting = get_arg(data, "expecting")
        if len(expecting) == 0: expecting = ALL_LEGAL_COMMANDS
        data = get_arg(data, "response")
    
    except Exception as e:
        return { "status": "err", "reason": str(e) }
    
    if rtype not in [ "action", "branch", "reply" ]: return { "status": "err", "reason": f"Invalid response type `{rtype}`." }

    expecting_fncall = rtype != "reply"

    candidates = data["candidates"]
    bad = False

    if len(candidates) == 0: bad = True
    else:
        content = candidates["content"]
        if "parts" not in content.keys(): bad = True
        else:
            parts = content["parts"]
            if len(parts) == 0: bad = True

    if bad: return agent_messed_up("No content found in response.")

    callidx = -1
    textidx = -1

    for (i, part) in enumerate(parts):

        if "functionCall" in part.keys():
            if callidx >= 0: return agent_messed_up("Only one function may be called per response.")
            callidx = i

        if "text" in part.keys():
            if textidx >= 0: return agent_messed_up("Only one text object may be provided per response.")
            textidx = i

    if expecting_fncall and callidx < 0: return agent_messed_up("No function call found in reply.")

    if not expecting_fncall and textidx < 0: return agent_messed_up("No text content found in reply.")

    if rtype == "action": 

        success, newctx, _ = run_action(
            content,
            callidx,
            proot,
            module,
            dgraph,
            expecting
        )
    
        return { 
            "status": "ok",
            "data": {
                "new_context": newctx,
                "agent_error": not success
            }
        }
    
    elif rtype == "answer":

        success, newctx, ans = run_action(
            content,
            callidx,
            proot,
            module,
            dgraph,
            expecting
        )
    
        return { 
            "status": "ok",
            "data": {
                "new_context": newctx,
                "answer": ans,
                "agent_error": not success
            }
        }

    else: 
        
        return { 
            "status": "ok", 
            "data": {
                "new_context": [content],
                "agent_error": False
            }
        }

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

        if request_type == "get_commands":
            response = get_commands()
        elif request_type == "handle_agent":
            response = handle_agent(data)
        else:
            response = { "status": "err", "reason": f"Unrecognized request `{request_type}`" }

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
