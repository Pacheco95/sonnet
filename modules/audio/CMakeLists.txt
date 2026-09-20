# miniaudio and stb_vorbis are single files without CMake packages; the implementation is
# compiled once, in src/MiniaudioImplementation.cpp.
find_path(MINIAUDIO_INCLUDE_DIR miniaudio.h REQUIRED)
find_package(Stb REQUIRED)
find_package(Threads REQUIRED)

sonnet_add_module(audio
  SOURCES
    src/Components.cpp
    src/MiniaudioDevice.cpp
    src/MiniaudioImplementation.cpp
  PUBLIC_DEPENDS
    sonnet::world
  DEPENDS
    # miniaudio loads the platform's audio libraries at run time and runs a thread of its own.
    Threads::Threads
    ${CMAKE_DL_LIBS}
)
target_include_directories(sonnet_audio SYSTEM PRIVATE ${MINIAUDIO_INCLUDE_DIR} ${Stb_INCLUDE_DIR})

sonnet_add_module_test(audio
  SOURCES
    tests/AudioTests.cpp
)
if(TARGET audio_tests)
  sonnet_add_engine_shaders(audio_tests)
endif()
