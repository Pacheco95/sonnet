find_package(fastgltf CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(Ktx CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(slang CONFIG REQUIRED)

sonnet_add_module(assets
  SOURCES
    src/Animation.cpp
    src/Asset.cpp
    src/AssetDatabase.cpp
    src/Bundle.cpp
    src/Cook.cpp
    src/GltfImporter.cpp
    src/ImageImporter.cpp
    src/Ktx2.cpp
    src/MeshCook.cpp
    src/Project.cpp
    src/ShaderCompiler.cpp
  PUBLIC_DEPENDS
    sonnet::renderer
    nlohmann_json::nlohmann_json
  DEPENDS
    fastgltf::fastgltf
    KTX::ktx
    slang::slang
)
target_include_directories(sonnet_assets SYSTEM PRIVATE ${Stb_INCLUDE_DIR})

sonnet_add_module_test(assets
  SOURCES
    tests/AnimationTests.cpp
    tests/BundleTests.cpp
    tests/CookTests.cpp
    tests/DatabaseTests.cpp
    tests/GltfTests.cpp
    tests/ProjectTests.cpp
    tests/ShaderTests.cpp
    tests/TextureTests.cpp
  DEPENDS
    nlohmann_json::nlohmann_json
)
if(TARGET assets_tests)
  sonnet_add_engine_shaders(assets_tests)
  target_include_directories(assets_tests SYSTEM PRIVATE ${Stb_INCLUDE_DIR})
  # The runtime compiler is tested on the engine's own sources.
  target_compile_definitions(assets_tests PRIVATE SONNET_ENGINE_SHADER_DIR="${SONNET_ENGINE_SHADER_DIR}")
endif()
