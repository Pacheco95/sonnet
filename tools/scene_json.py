"""The text of a scene or prefab file, as world::sceneText writes it (ADR-0029): two-space JSON
with every array of numbers, a vector, on one line. The scene generators write through it, so a
generated file looks like one the editor saved."""

import json


def dumps(node, indent=0):
    """`node` as scene file text, without the final newline."""
    if isinstance(node, list) and node and all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in node):
        return "[" + ", ".join(json.dumps(v) for v in node) + "]"
    inner = " " * (indent + 2)
    if isinstance(node, dict) and node:
        items = (f"{inner}{json.dumps(key)}: {dumps(value, indent + 2)}" for key, value in node.items())
        return "{\n" + ",\n".join(items) + "\n" + " " * indent + "}"
    if isinstance(node, list) and node:
        return "[\n" + ",\n".join(inner + dumps(value, indent + 2) for value in node) + "\n" + " " * indent + "]"
    return json.dumps(node)
