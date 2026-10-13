# Build options shared by every Poligon target (engine, game, examples, tests).
# Third-party dependencies do not link this target, so they keep their own warning levels.
add_library(poligon_build_options INTERFACE)

target_compile_options(poligon_build_options INTERFACE
  /W4          # high warning level
  /WX          # warnings are errors
  /permissive- # strict standard conformance
  /utf-8       # source and execution character sets are UTF-8
)
target_link_options(poligon_build_options INTERFACE /WX)

if(POLIGON_ANALYZE)
  target_compile_options(poligon_build_options INTERFACE /analyze /analyze:external-)
endif()

# AddressSanitizer has to cover every translation unit, so it is set globally.
if(POLIGON_ASAN)
  add_compile_options(/fsanitize=address)
  add_link_options(/INCREMENTAL:NO)

  # Sanitized programs need the sanitizer's runtime DLL. Put a copy next to them, so they
  # also start outside the build environment (for example from Explorer).
  set(asan_runtime "$ENV{VCToolsInstallDir}bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll")
  if(EXISTS "${asan_runtime}")
    file(COPY "${asan_runtime}" DESTINATION "${CMAKE_BINARY_DIR}/bin")
  else()
    message(WARNING "AddressSanitizer runtime DLL not found; sanitized programs will only start from the build environment.")
  endif()
endif()
