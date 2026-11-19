#pragma once

// A glTF of one mesh with two primitives, each a flat triangle in the XZ plane facing up: "Body" on the left
// (x from -2 to -0.2) in slot 0 and "Skin" on the right (x from 0.2 to 2) in slot 1, so a ray crosses
// one slot or the other (viewport drops on one submesh, docs/editor.md).
constexpr const char *TwoSlotFigure =
    R"json({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"name":"Figure","mesh":0}],)json"
    R"json("meshes":[{"name":"FigureMesh","primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0},)json"
    R"json({"attributes":{"POSITION":0},"indices":2,"material":1}]}],"materials":[{"name":"Body"},{"name":"Skin"}],)json"
    R"json("buffers":[{"uri":"data:application/octet-stream;base64,AAAAwAAAAAAAAIC/zcxMvgAAAAAAAIC/AAAAwAAAAAAAAIA/zcxMPgAAAAAAAIC/AAAAQAAAAAAAAIC/zcxMPgAAAAAAAIA/AAAAAAEAAAACAAAAAwAAAAQAAAAFAAAA","byteLength":96}],)json"
    R"json("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":72},{"buffer":0,"byteOffset":72,"byteLength":12},)json"
    R"json({"buffer":0,"byteOffset":84,"byteLength":12}],)json"
    R"json("accessors":[{"bufferView":0,"componentType":5126,"count":6,"type":"VEC3","min":[-2,0,-1],"max":[2,0,1]},)json"
    R"json({"bufferView":1,"componentType":5125,"count":3,"type":"SCALAR"},)json"
    R"json({"bufferView":2,"componentType":5125,"count":3,"type":"SCALAR"}]})json";
