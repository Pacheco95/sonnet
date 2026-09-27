#!/usr/bin/env python3
"""Rewrite a Slang SPIR-V module into the shapes Adreno's shader compiler accepts.

    python3 tools/spirv_for_adreno.py <in.spv> <out.spv>

Adreno 830's compiler (driver f61dec9117, compiler E031.47.18.50) fails on two things Slang emits
for the PhysicalStorageBuffer pointers in FrameConstants, although both are valid SPIR-V:

- A struct that uses a pointer type before the pointer is defined, which Slang declares ahead with
  OpTypeForwardPointer for every pointer to a struct. vkCreate*Pipelines dereferences null.
- A pointer to a struct computed with a dynamic element index (`lights[i]`) and then read at a
  member past the first, or loaded whole. vkCreate*Pipelines returns VK_ERROR_UNKNOWN. The same
  read works as one access chain from the root pointer (`OpPtrAccessChain lights i 1`).

So this pass folds every chain of access chains on a PhysicalStorageBuffer pointer into one chain
from its root, splits a load of a whole struct or array through such a chain into one load per
leaf and an OpCompositeConstruct, removes the chains left unused, and sorts the types, constants
and global variables so each comes after the ids it uses, which drops the forward declarations.
What the module computes does not change. A shape it does not know, such as a recursive pointer
type, a whole-struct store or a matrix loaded through a pointer, is an error rather than a module
that might crash the driver.

cmake/SonnetShaders.cmake runs it after slangc on Android only (docs/rendering.md, "Shaders").
"""

import struct
import sys

MAGIC = 0x07230203
PHYSICAL_STORAGE_BUFFER = 5349
ALIGNED = 0x2

OP_NAME, OP_EXT_INST = 5, 12
OP_TYPE_INT, OP_TYPE_FLOAT, OP_TYPE_VECTOR, OP_TYPE_MATRIX, OP_TYPE_IMAGE = 21, 22, 23, 24, 25
OP_TYPE_ARRAY, OP_TYPE_STRUCT, OP_TYPE_POINTER, OP_TYPE_FORWARD_POINTER = 28, 30, 32, 39
OP_CONSTANT, OP_SPEC_CONSTANT, OP_SPEC_CONSTANT_OP, OP_VARIABLE = 43, 50, 52, 59
OP_FUNCTION, OP_LOAD, OP_STORE = 54, 61, 62
OP_ACCESS_CHAIN, OP_IN_BOUNDS_ACCESS_CHAIN, OP_PTR_ACCESS_CHAIN = 65, 66, 67
OP_IN_BOUNDS_PTR_ACCESS_CHAIN = 70
OP_DECORATE, OP_COMPOSITE_CONSTRUCT = 71, 80
CHAINS = {OP_ACCESS_CHAIN, OP_IN_BOUNDS_ACCESS_CHAIN, OP_PTR_ACCESS_CHAIN, OP_IN_BOUNDS_PTR_ACCESS_CHAIN}
PTR_CHAINS = {OP_PTR_ACCESS_CHAIN, OP_IN_BOUNDS_PTR_ACCESS_CHAIN}

# The instructions of the types, constants and global variables section, and where their result
# id is: the type declarations carry it first, the rest after a result type.
TYPES = {19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 4472, 5341}
TYPED = {1, 12, 41, 42, 43, 44, 46, 48, 49, 50, 51, 52, 59}
SECTION = TYPES | TYPED | {OP_TYPE_FORWARD_POINTER}


def opcode(inst):
    return inst[0] & 0xFFFF


def make(op, *operands):
    return [((len(operands) + 1) << 16) | op, *operands]


def result_id(inst):
    return inst[1] if opcode(inst) in TYPES else inst[2]


class Module:
    def __init__(self, words):
        if len(words) < 5 or words[0] != MAGIC:
            raise ValueError("not a little-endian SPIR-V module")
        self.header = list(words[:5])
        self.insts = []
        index = 5
        while index < len(words):
            count = words[index] >> 16
            if count == 0 or index + count > len(words):
                raise ValueError(f"malformed instruction at word {index}")
            self.insts.append(list(words[index : index + count]))
            index += count
        first = next((i for i, inst in enumerate(self.insts) if opcode(inst) in SECTION), None)
        functions = next((i for i, inst in enumerate(self.insts) if opcode(inst) == OP_FUNCTION), len(self.insts))
        if first is None:
            raise ValueError("no types section")
        end = first
        while end < functions and opcode(self.insts[end]) in SECTION:
            end += 1
        if end != functions:
            raise ValueError(f"unexpected opcode {opcode(self.insts[end])} inside the types section")
        self.preamble = self.insts[:first]
        self.section = self.insts[first:end]
        self.functions = self.insts[end:]
        self.defined = {result_id(inst): inst for inst in self.section if opcode(inst) != OP_TYPE_FORWARD_POINTER}

    def words(self):
        return self.header + [word for inst in self.preamble + self.section + self.functions for word in inst]

    def new_id(self):
        self.header[3] += 1
        return self.header[3] - 1

    def add(self, inst):
        self.section.append(inst)
        self.defined[result_id(inst)] = inst
        return result_id(inst)

    def pointee(self, pointer_type):
        """The pointee of a PhysicalStorageBuffer pointer type, or None."""
        inst = self.defined.get(pointer_type)
        if inst and opcode(inst) == OP_TYPE_POINTER and inst[2] == PHYSICAL_STORAGE_BUFFER:
            return inst[3]
        return None

    def pointer_to(self, type_id):
        for inst in self.section:
            if opcode(inst) == OP_TYPE_POINTER and inst[2] == PHYSICAL_STORAGE_BUFFER and inst[3] == type_id:
                return inst[1]
        return self.add(make(OP_TYPE_POINTER, self.new_id(), PHYSICAL_STORAGE_BUFFER, type_id))

    def int_constant(self, value):
        int_type = next(
            (inst[1] for inst in self.section if opcode(inst) == OP_TYPE_INT and inst[2:] == [32, 1]), None
        ) or self.add(make(OP_TYPE_INT, self.new_id(), 32, 1))
        for inst in self.section:
            if opcode(inst) == OP_CONSTANT and inst[1] == int_type and inst[3:] == [value]:
                return inst[2]
        return self.add(make(OP_CONSTANT, int_type, self.new_id(), value))

    def constant_value(self, constant):
        inst = self.defined.get(constant)
        return inst[3] if inst and opcode(inst) == OP_CONSTANT and len(inst) == 4 else None

    def scalar_bytes(self, type_id):
        inst = self.defined[type_id]
        if opcode(inst) in (OP_TYPE_INT, OP_TYPE_FLOAT):
            return inst[2] // 8
        if opcode(inst) == OP_TYPE_VECTOR:
            return self.scalar_bytes(inst[2])
        if opcode(inst) == OP_TYPE_POINTER:
            return 8
        return 4


def fold_chains(module):
    """One access chain from the root pointer for every PhysicalStorageBuffer access."""
    chains = {}  # result id: (opcode of the folded chain, root pointer, indices)
    pointer_types = {}
    out = []
    for inst in module.functions:
        op = opcode(inst)
        if op in CHAINS and module.pointee(inst[1]) is not None:
            result_type, result, base, indices = inst[1], inst[2], inst[3], inst[4:]
            pointer_types[result] = result_type
            if base in chains:
                folded, root, prefix = chains[base]
                if op in PTR_CHAINS:
                    if module.constant_value(indices[0]) != 0:
                        raise ValueError(f"pointer arithmetic on %{base}, a pointer into a struct")
                    indices = indices[1:]
                chains[result] = (folded, root, prefix + indices)
            else:
                chains[result] = (OP_PTR_ACCESS_CHAIN if op in PTR_CHAINS else OP_ACCESS_CHAIN, base, indices)
            folded, root, indices = chains[result]
            out.append(make(folded, result_type, result, root, *indices))
        elif op == OP_LOAD and inst[3] in chains and opcode(module.defined[inst[1]]) != OP_TYPE_POINTER:
            loaded = module.defined[inst[1]]
            if opcode(loaded) in (OP_TYPE_STRUCT, OP_TYPE_ARRAY, OP_TYPE_MATRIX):
                split_load(module, chains[inst[3]], inst, out)
            else:
                out.append(inst)
        elif op == OP_STORE and inst[1] in chains:
            stored = module.pointee(pointer_types[inst[1]])
            if stored is not None and opcode(module.defined[stored]) in (OP_TYPE_STRUCT, OP_TYPE_ARRAY):
                raise ValueError(f"a whole struct or array stored through %{inst[1]}")
            out.append(inst)
        else:
            out.append(inst)
    module.functions = out
    remove_dead_chains(module)


def split_load(module, chain, load, out):
    folded, root, indices = chain
    memory = load[4:]

    def build(type_id, path, result):
        inst = module.defined[type_id]
        op = opcode(inst)
        if op == OP_TYPE_STRUCT:
            parts = [build(member, path + [module.int_constant(i)], None) for i, member in enumerate(inst[2:])]
        elif op == OP_TYPE_ARRAY:
            count = module.constant_value(inst[3])
            if count is None:
                raise ValueError(f"array %{type_id} has no constant length")
            parts = [build(inst[2], path + [module.int_constant(i)], None) for i in range(count)]
        elif op == OP_TYPE_MATRIX:
            raise ValueError(f"matrix %{type_id} loaded through a pointer; its stride would be lost")
        else:
            pointer = module.new_id()
            out.append(make(folded, module.pointer_to(type_id), pointer, root, *indices, *path))
            leaf = result if result is not None else module.new_id()
            operands = list(memory)
            if operands and operands[0] & ALIGNED:
                operands[1] = min(operands[1], module.scalar_bytes(type_id))
            out.append(make(OP_LOAD, type_id, leaf, pointer, *operands))
            return leaf
        composite = result if result is not None else module.new_id()
        out.append(make(OP_COMPOSITE_CONSTRUCT, type_id, composite, *parts))
        return composite

    build(load[1], [], load[2])


def remove_dead_chains(module):
    while True:
        used = set()
        for inst in module.functions:
            used.update(inst[1:])
            if opcode(inst) in CHAINS:
                used.discard(inst[2])
        dead = {inst[2] for inst in module.functions if opcode(inst) in CHAINS and inst[2] not in used}
        if not dead:
            break
        module.functions = [inst for inst in module.functions if not (opcode(inst) in CHAINS and inst[2] in dead)]
        module.preamble = [
            inst for inst in module.preamble if not (opcode(inst) in (OP_NAME, OP_DECORATE) and inst[1] in dead)
        ]


def used_ids(inst):
    """The operands of a types-section instruction that are ids: every one but these literals."""
    op = opcode(inst)
    operands = list(inst[1:])
    if op in (OP_TYPE_INT, OP_TYPE_FLOAT):
        return []
    if op in (OP_TYPE_VECTOR, OP_TYPE_MATRIX, OP_TYPE_IMAGE):
        return operands[1:2]
    if op == OP_TYPE_POINTER:
        return operands[2:]
    if op in TYPES:
        return operands[1:]
    # The result type, then what follows the result id.
    ids, rest = operands[:1], operands[2:]
    if op in (OP_CONSTANT, OP_SPEC_CONSTANT):
        return ids
    if op in (OP_SPEC_CONSTANT_OP, OP_VARIABLE):
        return ids + rest[1:]
    if op == OP_EXT_INST:
        # The set, the instruction number, then operands; every NonSemantic.Shader.DebugInfo.100
        # operand is an id, and no other set appears outside functions.
        return ids + rest[:1] + rest[2:]
    return ids + rest


def order_section(module):
    """Each type, constant and global variable after the ids it uses, and no forward pointers."""
    kept = [inst for inst in module.section if opcode(inst) != OP_TYPE_FORWARD_POINTER]
    defined = {result_id(inst): inst for inst in kept}
    placed, visiting, result = set(), set(), []

    def place(inst):
        rid = result_id(inst)
        if rid in placed:
            return
        if rid in visiting:
            raise ValueError(f"id %{rid} is part of a recursive type, which needs a forward pointer")
        visiting.add(rid)
        for used in used_ids(inst):
            if used in defined:
                place(defined[used])
        visiting.discard(rid)
        placed.add(rid)
        result.append(inst)

    for inst in kept:
        place(inst)
    module.section = result


def rewrite(words):
    module = Module(words)
    fold_chains(module)
    order_section(module)
    return module.words()


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    data = open(argv[1], "rb").read()
    if len(data) % 4:
        print(f"{argv[1]}: size is not a multiple of 4", file=sys.stderr)
        return 1
    try:
        words = rewrite(list(struct.unpack(f"<{len(data) // 4}I", data)))
    except ValueError as error:
        print(f"{argv[1]}: {error}", file=sys.stderr)
        return 1
    with open(argv[2], "wb") as out:
        out.write(struct.pack(f"<{len(words)}I", *words))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
