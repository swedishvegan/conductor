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

READ_PAGINATED_COMMAND = {
    "name": "read_paginated",
    "description": "Read file(s), segmented into logical pages",
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

EDIT_PAGE_COMMAND = {
    "name": "edit_page",
    "description": "Rewrite specified page(s) of a file. If you select to rewrite pages in the range [start_page, end_page] (inclusive), then the content you supply will directly replace these pages without modifying any other pages. **It is crucial that you take great care to provide the correct page indices, and provide content that correctly replaces the existing content of the pages in this range only.** Otherwise, you may unintentionally erase existing content or leave erroneous content in the file; either case could cause text to be illegible or cause code to not compile.",
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
                "description": "Content to write to the file page"
            },
            "start_page": {
                "type": "integer",
                "description": "First page to be replaced by your edit"
            },
            "end_page": {
                "type": "integer",
                "description": "Last page (inclusive) to be replaced by your edit"
            }
        },
        "required": ["module", "path", "content", "start_page", "end_page"]
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

answer_command = {
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
    "read_paginated": READ_PAGINATED_COMMAND,
    "write": WRITE_COMMAND,
    "append": APPEND_COMMAND,
    "edit_page": EDIT_PAGE_COMMAND,
    "query_modules": QUERY_MODULES_COMMAND,
    "create_module": CREATE_MODULE_COMMAND
} # `answer` is not considered a command; it is used for branching which is not a callable action

PAGE_SIZE = 50

def _gen_context_element(text, role):

    if role != "user" and role != "model": raise RuntimeError(f"Invalid role: {role}")

    return {
        "role": role,
        "parts": [
            { "text": str(text) }
        ]
    }

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
    if target in [".children", "*"]: return
    if target not in dgraph["modules"]: raise RuntimeError(f"Invalid module pattern `{target}`.")
    if not _can_read(module, target, dgraph) if accessty == "r" else _can_write(module, target, dgraph): 
        raise RuntimeError(f"Module `{module}` does not have permission to {'view the contents of' if accessty == 'r' else 'write to'} module `{target}`.")

def _get_modules(module, target, dgraph, accessty):
    _validate_fsop(module, target, dgraph, accessty)
    if target == ".dependencies": return dgraph["dependencies"][module]
    if target == ".children": return dgraph["children"][module]
    if target == ".": return module
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

def _get_pages(lines): return ["\n".join(lines[i:i + PAGE_SIZE]) for i in range(0, len(lines), PAGE_SIZE)]

def _read_file_pages(proot, target, path):
    with open(proot + target + "." + path, "r") as f:
        lines = f.readlines()
        return _get_pages(lines)

def _write_file_pages(proot, target, path, content, pages, spage, epage): #inclusive on both ends
    with open(proot + target + "." + path, "w") as f:
        newcontent = ""
        for (i, page) in enumerate(pages):
            if i < spage or i > epage: newcontent += page + ("\n" if i != len(pages) - 1 else "") # TODO: make sure this handles newlines correctly
            elif i == spage: newcontent += content + "\n" # only append content once
        f.write(newcontent) # erase old content and replace it with updated pages

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
        if path_arg not in dgraph["files"][module_arg]:
            raise RuntimeError(f"Module `{module_arg}` has no file called `{path_arg}`.")
        paths = [[module_arg, path_arg]]

    if len(paths) == 0: return None, [f"No files matched the pattern `{module_arg}/{path_arg}`."]
    return paths, None

def cmd_list(res, module, dgraph):
    
    module_arg = _get_arg(res, "module", module).strip()
    targets = _get_modules(module, module_arg, dgraph, "r")

    if len(targets) == 0: return [f"No modules matched the pattern `{module_arg}`."]

    return [
        f"Contents of module `{target}`:\n" + "\n".join(dgraph["files"][target])
        for target in targets
    ]

def cmd_read(proot, res, module, dgraph):

    paths, res = _setup_fsop(res, module, dgraph, "r")
    if res is not None: return res

    return [f"Contents of file `{path[0]}/{path[1]}`:\n" + _read_file(proot, path[0], path[1]) for path in paths]

def cmd_write_append(proot, res, module, dgraph, accessty):

    paths, res = _setup_fsop(res, module, dgraph, accessty)
    if res is not None: return res

    content = _get_arg(res, "content").strip()

    for path in paths: _write_file(proot, path[0], path[1], content, accessty)
    return []

def cmd_read_paginated(proot, res, module, dgraph):

    paths, res = _setup_fsop(res, module, dgraph, "r")
    if res is not None: return res

    res = []
    for path in paths:
        pages = _read_file_pages(proot, path[0], path[1])
        res.extend([f"Contents of file `{path[0]}/{path[1]}` page {p}:\n" + pages[p] for p in range(len(pages))]) # pages are zero-indexed not one-indexed, I think this is more natural

    return res

def cmd_edit_page(proot, res, module, dgraph):

    paths, res = _setup_fsop(res, module, dgraph, "w")
    if res is not None: return res

    content = _get_arg(res, "content").strip()
    try: spage = int(_get_arg(res, "start_page", -1))
    except: spage = -1
    try: epage = int(_get_arg(res, "end_page", -1))
    except: epage = -1

    for path in paths:
        pages = _read_file_pages(proot, path[0], path[1])
        if spage < 0 or epage >= len(pages) or spage > epage:
            raise RuntimeError(f"Invalid page range [{spage, epage}] for file `{path[0]}/{path[1]}` with {len(pages)} pages.")
        _write_file_pages(proot, path[0], path[1], content, pages, spage, epage)

    return []

def cmd_query_modules(proot, module, dgraph):

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
    )]

def cmd_create_module(res, module, dgraph):

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

    return []