#ifndef _commands_inc
#define _commands_inc

const char* LIST_COMMAND = R"RAW(
[{
  "function_declarations": [
    {
      "name": "LIST",
      "description": "List files",
      "parameters": {
        "type": "object",
        "properties": {
            "module": {
                "type": "string",
                "description": "Module to list the files in; defaults to current module if left blank"
            }
        },
        "required": []
      }
    }
  ]
}]
)RAW";

const char* READ_COMMAND = R"RAW(
[{
  "function_declarations": [
    {
      "name": "READ",
      "description": "Read file",
      "parameters": {
        "type": "object",
        "properties": {
          "module": {
            "type": "string",
            "description": "Module that the file resides in; defaults to current module if left blank"
          },
          "path": {
            "type": "string",
            "description": "Name of the file to read"
          }
        },
        "required": ["path"]
      }
    }
  ]
}]
)RAW";

const char* WRITE_COMMAND = R"RAW(
[{
  "function_declarations": [
    {
      "name": "WRITE",
      "description": "Write file",
      "parameters": {
        "type": "object",
        "properties": {
          "module": {
            "type": "string",
            "description": "Module that the file to write resides in; defaults to the current module if left blank" 
          },
          "path": {
            "type": "string",
            "description": "Name of the file to write"
          },
          "content": {
            "type": "string",
            "description": "Content to write to the file"
          }
        },
        "required": ["path", "content"]
      }
    }
  ]
}]
)RAW";

const char* APPEND_COMMAND = R"RAW(
[{
  "function_declarations": [
    {
      "name": "APPEND",
      "description": "Append to file",
      "parameters": {
        "type": "object",
        "properties": {
          "module": {
            "type": "string",
            "description": "Module that the file to append to resides in; defaults to the current module if left blank" 
          },
          "path": {
            "type": "string",
            "description": "Name of the file to append to"
          },
          "content": {
            "type": "string",
            "description": "Content to append to the file"
          }
        },
        "required": ["path", "content"]
      }
    }
  ]
}]
)RAW";

const char* LIST_MODULES_COMMAND = R"RAW(
[{
  "function_declarations": [
    {
      "name": "LIST_MODULES",
      "description": "List all existing modules, filtered into three subcategories: (1) children of the current module, (2) dependencies of the current module, (3) all other modules",
      "parameters": {
        "type": "object",
        "properties": {},
        "required": []
      }
    }
  ]
}]
)RAW";

const char* CREATE_MODULE_COMMAND = R"RAW(
[{
  "function_declarations": [
    {
      "name": "CREATE_MODULE",
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
  ]
}]
)RAW";

const char* ANSWER_COMMAND = R"RAW(
[{
  "function_declarations": [
    {
      "name": "ANSWER",
      "description": "Answer either YES or NO",
      "parameters": {
        "type": "object",
        "properties": {
          "answer": {
            "type": "string",
            "enum": ["YES", "NO"]
          }
        },
        "required": ["answer"]
      }
    }
  ]
}]
)RAW";

#endif