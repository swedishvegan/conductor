import json
import re

MODULE_DESCRIPTION_R = "Module name argument. Can be the name of any module that you have read-access to. Can also be one of the following special keywords: `.dependencies` denotes all dependencies of the current module; `.children` denotes all children of the current module; `.` denotes the current module; `*` denotes all modules that you have read-access to."

MODULE_DESCRIPTION_W = "Module name argument. Can be the name of any module that you have write-access to. Use `.` to denote the current module."

PATH_DESCRIPTION_R = "Name of the file. This argument supports the `*` wildcard, so patterns like `*.txt` may be used."

PATH_DESCRIPTION_W = "Name of the file. Wildcards not supported."

NO_OP_COMMAND = {
    "name": "no_op",
    "description": "Does nothing",
    "parameters": {
        "type": "object",
        "properties": {},
        "required": []
    }
}

LIST_COMMAND = {
    "name": "list",
    "description": "List files in specified module(s)",
    "parameters": {
        "type": "object",
        "properties": {
            "module": {
                "type": "string",
                "description": MODULE_DESCRIPTION_R
            }
        },
        "required": ["module"]
    }
}

READ_COMMAND = {
    "name": "read",
    "description": "Read file(s)",
    "parameters": {
        "type": "object",
        "properties": {
            "module": {
                "type": "string",
                "description": MODULE_DESCRIPTION_R
            },
            "path": {
                "type": "string",
                "description": PATH_DESCRIPTION_R
            }
        },
        "required": ["module", "path"]
    }
}

READ_LINES_COMMAND = {
    "name": "read_lines",
    "description": "Read file(s), segmented into individual numbered lines",
    "parameters": {
        "type": "object",
        "properties": {
            "module": {
                "type": "string",
                "description": MODULE_DESCRIPTION_R
            },
            "path": {
                "type": "string",
                "description": PATH_DESCRIPTION_R
            }
        },
        "required": ["module", "path"]
    }
}

WRITE_COMMAND = {
    "name": "write",
    "description": "Write file",
    "parameters": {
        "type": "object",
        "properties": {
            "module": {
                "type": "string",
                "description": MODULE_DESCRIPTION_W
            },
            "path": {
                "type": "string",
                "description": PATH_DESCRIPTION_W
            },
            "content": {
                "type": "string",
                "description": "Content to write to the file"
            }
        },
        "required": ["module", "path", "content"]
    }
}

APPEND_COMMAND = {
    "name": "append",
    "description": "Append to file",
    "parameters": {
        "type": "object",
        "properties": {
            "module": {
                "type": "string",
                "description": MODULE_DESCRIPTION_W
            },
            "path": {
                "type": "string",
                "description": PATH_DESCRIPTION_W
            },
            "content": {
                "type": "string",
                "description": "Content to append to the file"
            }
        },
        "required": ["module", "path", "content"]
    }
}

EDIT_COMMAND = {
    "name": "edit",
    "description": "Rewrite specified line(s) of a file. If you select to rewrite lines in the range [start_line, end_line] (inclusive), then the content you supply will directly replace these lines without modifying any other lines. **It is crucial that you take great care to provide the correct line indices, and provide content that correctly replaces the existing content of the lines in this range only.** Otherwise, you may unintentionally erase existing content or leave erroneous content in the file; either case could cause text to be illegible or cause code to not compile. The content you provide does not need to be a single line or have the same number of lines as the content being replaced.",
    "parameters": {
        "type": "object",
        "properties": {
            "module": {
                "type": "string",
                "description": MODULE_DESCRIPTION_W
            },
            "path": {
                "type": "string",
                "description": PATH_DESCRIPTION_W
            },
            "new_lines": {
                "type": "array",
                "items": {"type": "string"},
                "description": "Content that will replace the specified file line range"
            },
            "start_line": {
                "type": "integer",
                "description": "First line to be replaced by your edit"
            },
            "end_line": {
                "type": "integer",
                "description": "Last line (inclusive) to be replaced by your edit; if set to -1, it is treated as the same value as start_line"
            }
        },
        "required": ["module", "path", "new_lines", "start_line", "end_line"]
    }
}

QUERY_MODULES_COMMAND = {
    "name": "query_modules",
    "description": "List all existing modules, filtered into three subcategories: (1) children of the current module, (2) dependencies of the current module, (3) all other modules",
    "parameters": {
        "type": "object",
        "properties": {},
        "required": []
    }
}

CREATE_MODULE_COMMAND = {
    "name": "create_module",
    "description": "Create a new module and explicitly declare its dependencies",
    "parameters": {
        "type": "object",
        "properties": {
            "dependencies": {
                "type": "array",
                "items": {"type": "string"},
                "description": "Dependencies may ONLY include (1) the current module, or (2) any dependencies of the current module; allowed to be an empty array"
            },
            "module_name": {
                "type": "string",
                "description": "Must not clash with any existing module names"
            }
        },
        "required": ["module_name", "dependencies"]
    }
}

ANSWER_COMMAND = {
    "name": "answer",
    "description": "Answer either `yes` or `no`",
    "parameters": {
        "type": "object",
        "properties": {
            "answer": {
                "type": "string",
                "enum": ["yes", "no"]
            }
        },
        "required": ["answer"]
    }
}

DEFAULT_COMMANDS = {
    "no_op": NO_OP_COMMAND,
    "list": LIST_COMMAND,
    "read": READ_COMMAND,
    "read_lines": READ_LINES_COMMAND,
    "write": WRITE_COMMAND,
    "append": APPEND_COMMAND,
    "edit": EDIT_COMMAND,
    "query_modules": QUERY_MODULES_COMMAND,
    "create_module": CREATE_MODULE_COMMAND,
    "answer": ANSWER_COMMAND
} # `answer` is not considered a command; it is used for branching which is not a callable action

PAGE_SIZE = 50

def _get_arg(res, arg, default=""):
    return res[arg] if arg in res.keys() else default

def _can_read(module, target, dgraph):
    return module == target or target == "global" or target in dgraph["children"][module] or target in dgraph["dependencies"][module]

def _can_write(module, target, dgraph):
    return module == target or target == "global" or target in dgraph["children"][module]

def _validate_fsop(module, target, dgraph, accessty):
    if target == ".dependencies":
        if accessty != "r": raise RuntimeError(f"Dependency modules are read-only.")
        return
    if target in [".children", "*", "."]: return
    if target not in dgraph["modules"]: raise RuntimeError(f"Invalid module pattern `{target}`.")
    if not _can_read(module, target, dgraph) if accessty == "r" else _can_write(module, target, dgraph): 
        raise RuntimeError(f"Module `{module}` does not have permission to {'view the contents of' if accessty == 'r' else 'write to'} module `{target}`.")

def _get_modules(module, target, dgraph, accessty):
    _validate_fsop(module, target, dgraph, accessty)
    if target == ".dependencies": return dgraph["dependencies"][module]
    if target == ".children": return dgraph["children"][module]
    if target == ".": return [module]
    if target == "*": return [module, "global"] + dgraph["children"][module] + (dgraph["dependencies"][module] if accessty == "r" else [])
    return [target]

def _format_re(arg):
    return arg.replace(".", "\\.").replace("*", ".*")

def _populate_paths(target, pattern, args, candidates):
    for c in candidates:
        if re.fullmatch(pattern, c) is not None:
            args.append([target, c])

def _read_file(proot, target, path):
    with open(proot + target + "." + path, "r") as f: return f.read()

def _write_file(proot, target, path, content, accessty):
    with open(proot + target + "." + path, accessty) as f: f.write(content)


def _read_file_lines(proot, target, path):
    with open(proot + target + "." + path, "r") as f:
        return f.readlines()

def _write_file_lines(proot, target, path, content, lines, sline, eline): #inclusive on both ends
    with open(proot + target + "." + path, "w") as f:
        newcontent = ""
        for (i, page) in enumerate(lines):
            if i < sline or i > eline: newcontent += page
            elif i == sline: newcontent += content + "\n" # only append content once
        f.write(newcontent) # erase old content and replace it with updated lines

def _setup_fsop(res, module, dgraph, accessty):

    module_arg = _get_arg(res, "module", module).strip()
    path_arg = _get_arg(res, "path").strip()

    if accessty == "r":
        targets = _get_modules(module, module_arg, dgraph, accessty)
        if len(targets) == 0: return None, [f"No modules matched the pattern `{module_arg}`."]
        path_arg_f = _format_re(path_arg)
        paths = []
        for target in targets: _populate_paths(target, path_arg_f, paths, dgraph["files"][target])
    else:
        if module_arg == ".": module_arg = module
        if module_arg not in dgraph["modules"]:
            raise RuntimeError(f"Module `{module_arg}` does not exist.")
        if not _can_write(module, module_arg, dgraph):
            raise RuntimeError(f"Module `{module}` does not have permission to write to module `{module_arg}.`")
        if accessty == "e" and path_arg not in dgraph["files"][module_arg]:
            raise RuntimeError(f"Module `{module_arg}` does not contain file `{path_arg}`.")
        paths = [[module_arg, path_arg]]

    if len(paths) == 0: return None, [f"No files matched the pattern `{module_arg}/{path_arg}`."]
    return paths, None

# implementations of default commands
# some arguments are dummy arguments in many 
# functions, but they're there so every
# function has the same API can can be called
# homogenously

def cmd_no_op(proot, res, module, dgraph):
    return [ "Successfully done nothing." ], False

def cmd_list(proot, res, module, dgraph):
    
    module_arg = _get_arg(res, "module", module).strip()
    targets = _get_modules(module, module_arg, dgraph, "r")

    if len(targets) == 0: return [f"No modules matched the pattern `{module_arg}`."], False

    return [
        f"Contents of module `{target}`:\n" + "\n".join(dgraph["files"][target]) if len(dgraph["files"][target]) > 0 else f"Module `{target}` is empty."
        for target in targets
    ], False

def cmd_read(proot, res, module, dgraph):

    paths, problem = _setup_fsop(res, module, dgraph, "r")
    if problem is not None: return problem, False

    return [f"Contents of file `{path[0]}/{path[1]}`:\n" + _read_file(proot, path[0], path[1]) for path in paths], False

def _cmd_write_append(proot, res, module, dgraph, accessty):

    paths, problem = _setup_fsop(res, module, dgraph, accessty)
    if problem is not None: return problem, False

    content = _get_arg(res, "content").strip()
    dgraph_updated = False

    for path in paths:

        module_arg, path_arg = path
        _write_file(proot, module_arg, path_arg, content, accessty)

        # update dependency graph
        existing_files = dgraph["files"][module_arg]
        if path_arg not in existing_files: 
            
            existing_files.append(path_arg)
            dgraph_updated = True

    return [f"Content successfully written to `{paths[0][0]}/{paths[0][1]}`."], dgraph_updated

def cmd_write(proot, res, module, dgraph):
    return _cmd_write_append(proot, res, module, dgraph, "w")

def cmd_append(proot, res, module, dgraph):
    return _cmd_write_append(proot, res, module, dgraph, "a")

def cmd_read_lines(proot, res, module, dgraph):

    paths, problem = _setup_fsop(res, module, dgraph, "r")
    if problem is not None: return problem, False

    res = []
    for path in paths:
        lines = _read_file_lines(proot, path[0], path[1])
        res.append("\n".join([f"Line {ln}: ```{lines[ln]}```" for ln in range(len(lines))]))

    return res, False

def cmd_edit(proot, res, module, dgraph):

    paths, problem = _setup_fsop(res, module, dgraph, "e")
    if problem is not None: return problem, False

    new_lines = _get_arg(res, "new_lines", [])
    try: sline = int(_get_arg(res, "start_line", -1))
    except: sline = -1
    try: eline = int(_get_arg(res, "end_line", -1))
    except: eline = -1

    if eline == -1: eline = sline

    for path in paths:
        lines = _read_file_lines(proot, path[0], path[1])
        if sline < 0 or eline >= len(lines) or sline > eline:
            raise RuntimeError(f"Invalid line range [{sline, eline}] for file `{path[0]}/{path[1]}` with {len(lines)} lines.")
        _write_file_lines(proot, path[0], path[1], "\n".join(new_lines), lines, sline, eline)

    return [f"Successfully edited lines {sline}-{eline} of `{paths[0][0]}/{paths[0][1]}`."], False

def cmd_query_modules(proot, res, module, dgraph):

    deps = dgraph["dependencies"][module]
    children = dgraph["children"][module]
    allmodules = dgraph["modules"]
    return [(
        f"Current module: `{module}`\nDependencies of current module: `" + 
        ("`, `".join(deps) if len(deps) > 0 else "[none]") +
        "`\nChildren of current module: `" + 
        ("`, `".join(children) if len(children) > 0 else "[none]") +
        "`\nAll modules: `" +
        "`, `".join(allmodules) + "`"
    )], False

def cmd_create_module(proot, res, module, dgraph):

    module_arg = _get_arg(res, "module_name").strip()

    if len(module_arg) == 0 or module_arg.isspace(): raise RuntimeError("Module name missing or empty.")
    if module_arg in dgraph["modules"]: raise RuntimeError(f"There is already a module named `{module_arg}`.")

    deps_arg = _get_arg(res, "dependencies", [])
    deps_arg = [ dep.strip() for dep in deps_arg ]

    for dep in deps_arg:
        if not (dep in dgraph["dependencies"][module] or dep in dgraph["children"][module] or dep == module):
            raise RuntimeError(f"Invalid dependency module `{dep}`.")

    dgraph["modules"].append(module_arg)
    dgraph["dependencies"][module_arg] = deps_arg
    dgraph["children"][module_arg] = []
    dgraph["files"][module_arg] = []
    dgraph["children"][module].append(module_arg)
    #print(f"Updated dgraph = {dgraph}")
    return [f"Successfully created module `{module_arg}`."], True

def cmd_answer(proot, res, module, dgraph):

    answer_arg = _get_arg(res, "answer").strip().lower()
    if len(answer_arg) == 0 or answer_arg.isspace(): raise RuntimeError("Answer is missing or empty.")

    if answer_arg == "yes": return True
    elif answer_arg == "no": return False
    else: raise RuntimeError("Answer must be either `yes` or `no`.")

DEFAULT_ACTIONS = {
    "no_op": cmd_no_op,
    "list": cmd_list,
    "read": cmd_read,
    "write": cmd_write,
    "append": cmd_append,
    "read_lines": cmd_read_lines,
    "edit": cmd_edit,
    "query_modules": cmd_query_modules,
    "create_module": cmd_create_module,
    "answer": cmd_answer
}