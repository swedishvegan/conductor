
# chatgpt

from __future__ import annotations

from typing import Any, Dict, Mapping, Optional, Set

def validate_arguments(
    args: Mapping[str, Any],
    function_declaration: Mapping[str, Any],
) -> Dict[str, Dict[str, Optional[bool]]]:
    """
    Recursively validate *args* against a Gemini‑style function-declaration
    schema (assuming parameters.type == "object").

    Returns a dict keyed by each TOP‑LEVEL parameter name that appears EITHER
    in the schema OR in args.  Each entry is:

        {
          "exists": bool,          # key present in *args*
          "valid": Optional[bool]  # True / False / None (None → unknown key)
        }
    """
    params = function_declaration.get("parameters", {})
    if (params.get("type") or "").lower() != "object":
        raise ValueError("This helper assumes parameters.type == 'object'.")

    properties: Mapping[str, Any] = params.get("properties", {}) or {}
    required: Set[str] = set(params.get("required", []) or [])

    results: Dict[str, Dict[str, Optional[bool]]] = {}

    # First handle keys we expect (in the declaration)
    for name, subschema in properties.items():
        exists = name in args
        if not exists:
            # Missing → valid only if not required
            valid: Optional[bool] = name not in required
        else:
            valid = _validate_value(args[name], subschema)

        results[name] = {"exists": exists, "valid": valid}

    # Now surface *unexpected* keys from args
    unexpected_keys = set(args.keys()) - set(properties.keys())
    for extra in unexpected_keys:
        # exists = True, valid = None to flag "not in schema"
        results[extra] = {"exists": True, "valid": None}

    return results


# --------------------------------------------------------------------------- #
# Helper: recursive value‑vs‑schema validation                                #
# --------------------------------------------------------------------------- #
def _validate_value(value: Any, schema: Mapping[str, Any]) -> bool:
    """
    Validate *value* recursively against a subset of OpenAPI / Gemini schema.

    Supported facets:
      - type: string | integer | number | boolean | array | object
      - nullable
      - enum
      - object.properties + required
      - array.items
    """

    # --------------------------- 1. nullable ------------------------------- #
    if value is None:
        return bool(schema.get("nullable", False))  # True ↔ explicitly nullable

    # ----------------------------- 2. enum --------------------------------- #
    if "enum" in schema:
        # enum is treated as a closed set
        try:
            if value not in schema["enum"]:
                return False
        except TypeError:
            # Non‑hashable value cannot be in enum
            return False

    # ------------------------ 3. primitive types --------------------------- #
    schema_type = (schema.get("type") or "").lower()

    if schema_type == "string":
        return isinstance(value, str)

    if schema_type == "integer":
        return isinstance(value, int) and not isinstance(value, bool)

    if schema_type == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)

    if schema_type == "boolean":
        return isinstance(value, bool)

    # --------------------------- 4. array ---------------------------------- #
    if schema_type == "array":
        if not isinstance(value, list):
            return False
        item_schema = schema.get("items")
        if not item_schema:
            # No item type specified → any list is OK
            return True
        return all(_validate_value(elem, item_schema) for elem in value)

    # --------------------------- 5. object --------------------------------- #
    if schema_type == "object":
        if not isinstance(value, dict):
            return False
        props: Mapping[str, Any] = schema.get("properties", {}) or {}
        required: Set[str] = set(schema.get("required", []) or [])

        # All required keys must exist
        if not required.issubset(value.keys()):
            return False

        # Validate known keys
        for key, subschema in props.items():
            if key in value and not _validate_value(value[key], subschema):
                return False

        # Unknown nested keys are allowed (loose object validation)
        return True

    # ---------------------- 6. unrecognised type --------------------------- #
    # Treat as valid (or raise, if you prefer stricter behaviour)
    return True

if __name__ == "__main__":
    # 1. Required missing
    decl1 = {
        "name": "req_missing",
        "parameters": {
            "type": "object",
            "properties": {"a": {"type": "string"}},
            "required": ["a"]
        }
    }
    print("1:", validate_arguments({}, decl1))
    # Expect: a → exists=False, valid=False

    # 2. Nullable fields
    decl2 = {
        "name": "nullable",
        "parameters": {
            "type": "object",
            "properties": {
                "a": {"type": "string", "nullable": True},
                "b": {"type": "integer"}
            }
        }
    }
    print("2:", validate_arguments({"a": None}, decl2))
    # Expect: a → exists=True, valid=True; b → exists=False, valid=True

    # 3. Enum
    decl3 = {
        "name": "enum_test",
        "parameters": {
            "type": "object",
            "properties": {
                "color": {"type": "string", "enum": ["red", "green", "blue"]}
            }
        }
    }
    print("3a:", validate_arguments({"color": "green"}, decl3))
    print("3b:", validate_arguments({"color": "yellow"}, decl3))
    # Expect 3a: valid True; 3b: valid False

    # 4. Nested object (child missing required)
    decl4 = {
        "name": "nested_object",
        "parameters": {
            "type": "object",
            "properties": {
                "config": {
                    "type": "object",
                    "properties": {
                        "mode": {"type": "string"},
                        "level": {"type": "integer"}
                    },
                    "required": ["mode"]
                }
            }
        }
    }
    print("4a:", validate_arguments({"config": {"mode": "safe", "level": 1}}, decl4))
    print("4b:", validate_arguments({"config": {"level": 1}}, decl4))
    # Expect 4a: valid True; 4b: valid False

    # 5. Array of objects
    decl5 = {
        "name": "array_objects",
        "parameters": {
            "type": "object",
            "properties": {
                "points": {
                    "type": "array",
                    "items": {
                        "type": "object",
                        "properties": {
                            "x": {"type": "number"},
                            "y": {"type": "number"}
                        },
                        "required": ["x", "y"]
                    }
                }
            }
        }
    }
    print("5a:", validate_arguments({"points": [{"x": 1.0, "y": 2.0}, {"x": 3, "y": 4}]}, decl5))
    print("5b:", validate_arguments({"points": [{"x": 1.0}, {"x": 3, "y": 4}]}, decl5))
    # Expect 5a: valid True; 5b: valid False

    # 6. Unexpected keys at top-level
    decl6 = {
        "name": "unexpected",
        "parameters": {
            "type": "object",
            "properties": {"a": {"type": "string"}}
        }
    }
    print("6:", validate_arguments({"b": 123}, decl6))
    # Expect: a → exists=False, valid=True; b → exists=True, valid=None

    # 7. Type mismatches
    decl7 = {
        "name": "types",
        "parameters": {
            "type": "object",
            "properties": {
                "n": {"type": "number"},
                "i": {"type": "integer"},
                "b": {"type": "boolean"},
                "s": {"type": "string"}
            }
        }
    }
    print("7:", validate_arguments({"n": True, "i": 5.5, "b": "nope", "s": 7}, decl7))
    # Expect: n False (bool not allowed for number), i False, b False, s False

    # 8. Nested object with extra keys (extra should not break validity)
    decl8 = {
        "name": "nested_extra",
        "parameters": {
            "type": "object",
            "properties": {
                "settings": {
                    "type": "object",
                    "properties": {
                        "mode": {"type": "string"}
                    }
                }
            }
        }
    }
    print("8:", validate_arguments({"settings": {"mode": "on", "extra": 999}}, decl8))
    # Expect: settings exists=True, valid=True