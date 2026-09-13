# Warning flags for engine targets. Third-party code never sees them: vcpkg include
# directories are SYSTEM, and the flags are attached per engine target, never globally.

add_library(sonnet_warnings INTERFACE)
add_library(sonnet::warnings ALIAS sonnet_warnings)

if(MSVC)
  target_compile_options(sonnet_warnings INTERFACE
    /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8 /EHsc
    /w14242 /w14254 /w14263 /w14265 /w14287 /w14296 /w14311 /w14545 /w14546 /w14547 /w14549 /w14555
    /w14619 /w14640 /w14826 /w14905 /w14906 /w14928)
else()
  target_compile_options(sonnet_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor -Wold-style-cast
    -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion -Wimplicit-fallthrough -Wcast-align -Wunused
    -Wformat=2)
  # Repository-relative file names in __FILE__ and std::source_location, so log lines read the same
  # on every machine. Debug info keeps absolute paths: -ffile-prefix-map would also rewrite DWARF's
  # DW_AT_name and DW_AT_comp_dir to relative paths, and debuggers then cannot bind breakpoints to
  # the source files.
  target_compile_options(sonnet_warnings INTERFACE "-fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=")
endif()

# COMPILE_WARNING_AS_ERROR has no INTERFACE_ form; the module, test and executable helpers set it
# on every engine target they create.
