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
endif()
