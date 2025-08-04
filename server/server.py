
from fsop import DEFAULT_COMMANDS, DEFAULT_ACTIONS

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

def run_agent_action(content, partidx, proot, module, dgraph, default_params, expecting):

    part = content["parts"][partidx]["functionCall"]
    actname = part["name"]
    res = part["args"]
    newctx = [content]

    if actname in default_params.keys():
        res = res | default_params[actname]

    try:

        if actname not in expecting:
            raise RuntimeError(f"Agent was suppposed to call one of the following functions: `{'`, `'.join(expecting)}` in this reply, but instead attempted to call `{actname}`.")

        if actname == "answer":

            res = DEFAULT_ACTIONS["answer"](None, res, None, None)
            return True, newctx, res, None

        elif actname in DEFAULT_COMMANDS:

            res, update_dgraph = DEFAULT_ACTIONS[actname](proot, res, module, dgraph)
            newctx.extend(
                convert_fsop_output(res)
            )
            return True, newctx, None, update_dgraph

    except Exception as e:
        newctx.extend(
            convert_fsop_output([f"Error: {str(e)}"])
        )
        return False, newctx, False, False

    raise RuntimeError("Custom actions not yet implemented.")


def get_arg(d, a):

    try: return d[a]
    except: raise RuntimeError(f"Required argument `{a}` is missing from request")

def agent_messed_up(content, reason):
    return {
        "status": "ok",
        "data": {
            "new_context": ([content] if content is not None else []) + convert_fsop_output(["Error: " + reason]),
            "agent_error": True
        }
    }

def handle_agent(data):

    try:

        rtype = get_arg(data, "response_type")
        proot = get_arg(data, "project_root")
        module = get_arg(data, "module")
        dgraph = get_arg(data, "dependency_graph")
        default_params = get_arg(data, "default_parameters") # for handle_agent, actions is a dict, not a list like in run_user_action; this is because the actions have a slightly different meaning in this context
        expecting = get_arg(data, "expecting")
        if len(expecting) == 0: expecting = ALL_LEGAL_COMMANDS
        data = get_arg(data, "response")
        data = json.loads(data) # data is passed from client still in string form to prevent a needless conversion to/from JSON
    
    except Exception as e:
        return { "status": "err", "reason": str(e) }

    if rtype not in [ "action", "branch", "reply" ]: return { "status": "err", "reason": f"Invalid response type `{rtype}`." }
    
    expecting_fncall = rtype != "reply"

    candidates = data["candidates"]
    bad = False
    content = None

    if len(candidates) == 0: bad = True
    else:
        try:
            candidates = candidates[0]
            content = candidates["content"]
            if "parts" not in content.keys(): bad = True
            else:
                parts = content["parts"]
                if len(parts) == 0: bad = True
        except: bad = True

    if bad: return agent_messed_up(content, "No content found in response.")

    callidx = -1
    textidx = -1

    for (i, part) in enumerate(parts):

        if "functionCall" in part.keys():
            if callidx >= 0: return agent_messed_up(content, "Only one function may be called per response.")
            callidx = i

        if "text" in part.keys():
            if textidx >= 0: return agent_messed_up(content, "Only one text object may be provided per response.")
            textidx = i

    if expecting_fncall and callidx < 0: return agent_messed_up(content, "No function call found in reply.")

    if not expecting_fncall and textidx < 0: return agent_messed_up(content, "No text content found in reply.")

    if rtype == "action": 

        success, newctx, _, update_dgraph = run_agent_action(
            content,
            callidx,
            proot,
            module,
            dgraph,
            default_params,
            expecting
        )
    
        r = { 
            "status": "ok",
            "data": {
                "new_context": newctx,
                "agent_error": not success
            }
        }

        if update_dgraph: r["data"]["dependency_graph"] = dgraph
        
        return r
    
    elif rtype == "branch":

        success, newctx, ans, _ = run_agent_action(
            content,
            callidx,
            proot,
            module,
            dgraph,
            default_params,
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

def run_user_action(data):

    try:

        proot = get_arg(data, "project_root")
        module = get_arg(data, "module")
        dgraph = get_arg(data, "dependency_graph")
        actions = get_arg(data, "actions")
    
    except Exception as e:
        return { "status": "err", "reason": str(e) }

    newctx = []

    for action in actions:

        try:

            name = get_arg(action, "name")
            args = get_arg(action, "args")
            res, update_dgraph = DEFAULT_ACTIONS[name](proot, args, module, dgraph)

        except Exception as e:
            return { "status": "err", "reason": str(e) }

        newctx.extend(res)

    r = {
        "status": "ok",
        "data": {
            "new_context": convert_fsop_output(newctx)
        }
    }

    if update_dgraph: r["data"]["dependency_graph"] = dgraph

    return r
    
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
        print(f"Received JSON: {data}")

        request_type = data["request"]
        data = data["data"]

        if request_type == "get_commands":
            response = get_commands()
        elif request_type == "handle_agent":
            response = handle_agent(data)
        elif request_type == "run_user_action":
            response = run_user_action(data)
        else:
            response = { "status": "err", "reason": f"Unrecognized request `{request_type}`" }

        if response["status"] == "ok":
            print("\nHandled request with no issues\n\n")
        else:
            print(f"\nFailed to handle request: {response['reason']}\n\n")

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
    print(f"Server PID written to {PIDFILE}\n\n")

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
