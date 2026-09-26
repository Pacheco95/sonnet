#!/usr/bin/env python3
"""Check tools/spirv_for_adreno.py against compiled modules.

    python3 tools/test_spirv_for_adreno.py <directory of .spv modules>

For every module it rewrites, it checks that nothing Adreno rejects is left: no forward pointer,
no type before an id it uses, no access chain on a PhysicalStorageBuffer pointer whose base is
another such chain, and no whole struct or array loaded through one. The rewrite must also be
idempotent. When spirv-val is on PATH (the Vulkan SDK has it), the result must validate as the
engine's device takes it. The renderer's test registers this against the engine's modules.
"""

import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

# The test runs from the source tree; it must not leave __pycache__ there.
sys.dont_write_bytecode = True
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import spirv_for_adreno as adreno  # noqa: E402


def problems(words):
    module = adreno.Module(words)
    found = []
    placed = set()
    for inst in module.section:
        op = adreno.opcode(inst)
        if op == adreno.OP_TYPE_FORWARD_POINTER:
            found.append("a forward pointer")
            continue
        late = [used for used in adreno.used_ids(inst) if used in module.defined and used not in placed]
        if late:
            found.append(f"%{adreno.result_id(inst)} uses %{late[0]} before it is defined")
        placed.add(adreno.result_id(inst))
    chains = set()
    for inst in module.functions:
        op = adreno.opcode(inst)
        if op in adreno.CHAINS and module.pointee(inst[1]) is not None:
            if inst[3] in chains:
                found.append(f"chain %{inst[2]} is based on chain %{inst[3]}")
            chains.add(inst[2])
        elif op == adreno.OP_LOAD and inst[3] in chains:
            if adreno.opcode(module.defined[inst[1]]) in (adreno.OP_TYPE_STRUCT, adreno.OP_TYPE_ARRAY):
                found.append(f"%{inst[2]} loads a whole composite through chain %{inst[3]}")
    return found


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    modules = sorted(pathlib.Path(argv[1]).glob("*.spv"))
    if not modules:
        print(f"no .spv modules in {argv[1]}", file=sys.stderr)
        return 1
    validator = shutil.which("spirv-val")
    failures = 0
    folded = 0
    for path in modules:
        data = path.read_bytes()
        words = list(struct.unpack(f"<{len(data) // 4}I", data))
        rewritten = adreno.rewrite(words)
        found = problems(rewritten)
        if adreno.rewrite(rewritten) != rewritten:
            found.append("rewriting twice changes the module")
        if validator:
            with tempfile.TemporaryDirectory() as scratch:
                out = pathlib.Path(scratch) / path.name
                out.write_bytes(struct.pack(f"<{len(rewritten)}I", *rewritten))
                run = subprocess.run(
                    [validator, "--target-env", "vulkan1.3", "--scalar-block-layout", str(out)],
                    capture_output=True,
                    text=True,
                )
                if run.returncode != 0:
                    found.append(f"spirv-val: {run.stderr.strip() or run.stdout.strip()}")
        if problems(words):
            folded += 1
        for problem in found:
            print(f"{path.name}: {problem}", file=sys.stderr)
        failures += bool(found)
    print(
        f"{len(modules)} modules, {folded} rewritten, {failures} failing"
        + ("" if validator else " (spirv-val not found, not validated)")
    )
    # The engine's modules do contain both shapes; a run that rewrites none has lost its subject.
    return 1 if failures or folded == 0 else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
