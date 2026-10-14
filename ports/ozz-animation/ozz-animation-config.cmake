# Package file for ozz-animation as installed by Poligon's vcpkg port (DLLs).
# Targets: ozz::base, ozz::animation (runtime: sampling, blending, inverse kinematics),
# ozz::animation_offline (builds runtime skeletons and animations from plain data).
get_filename_component(_ozz_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

function(_ozz_add_library target library)
  if(TARGET ozz::${target})
    return()
  endif()
  add_library(ozz::${target} SHARED IMPORTED)
  set_target_properties(ozz::${target} PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_ozz_root}/include"
    # Makes ozz's headers declare its functions as imported from a DLL.
    INTERFACE_COMPILE_DEFINITIONS OZZ_USE_DYNAMIC_LINKING
    IMPORTED_CONFIGURATIONS "RELEASE;DEBUG"
    IMPORTED_IMPLIB_RELEASE "${_ozz_root}/lib/${library}.lib"
    IMPORTED_LOCATION_RELEASE "${_ozz_root}/bin/${library}.dll"
    IMPORTED_IMPLIB_DEBUG "${_ozz_root}/debug/lib/${library}.lib"
    IMPORTED_LOCATION_DEBUG "${_ozz_root}/debug/bin/${library}.dll"
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    MAP_IMPORTED_CONFIG_MINSIZEREL Release)
endfunction()

_ozz_add_library(base ozz_base)
_ozz_add_library(animation ozz_animation)
_ozz_add_library(animation_offline ozz_animation_offline)
set_property(TARGET ozz::animation APPEND PROPERTY INTERFACE_LINK_LIBRARIES ozz::base)
set_property(TARGET ozz::animation_offline APPEND PROPERTY INTERFACE_LINK_LIBRARIES ozz::animation ozz::base)
