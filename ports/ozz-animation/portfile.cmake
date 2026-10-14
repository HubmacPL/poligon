# ozz-animation is not in the vcpkg catalog, so Poligon carries this small port of its own.
# It builds the libraries only, pinned to one release by version and checksum.
#
# The libraries are built as DLLs, like the other dependencies in the x64-windows triplet.
# As static libraries they could not be linked into the AddressSanitizer build: the linker
# refuses to mix our sanitized code with their unsanitized standard containers.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO guillaumeblanc/ozz-animation
    REF "${VERSION}"
    SHA512 85e6c1b5e693c2f044815b92cb0896fb7f7d495d91cb9ff55aef4615483c33c3e4ef7232f84aa6539c92aed0e9d842fcc448712166a0eab2b252525196caff90
    HEAD_REF master
)

vcpkg_check_linkage(ONLY_DYNAMIC_LIBRARY)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -Dozz_build_tools=OFF
        -Dozz_build_fbx=OFF
        -Dozz_build_gltf=OFF
        -Dozz_build_samples=OFF
        -Dozz_build_howtos=OFF
        -Dozz_build_tests=OFF
        -Dozz_build_postfix=OFF
        -Dozz_build_msvc_rt_dll=ON
)
vcpkg_cmake_install()

# ozz installs everything into lib; vcpkg wants DLLs in bin.
foreach(prefix "" "/debug")
    file(GLOB dlls "${CURRENT_PACKAGES_DIR}${prefix}/lib/*.dll")
    if(dlls)
        file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}${prefix}/bin")
        foreach(dll IN LISTS dlls)
            get_filename_component(name "${dll}" NAME)
            file(RENAME "${dll}" "${CURRENT_PACKAGES_DIR}${prefix}/bin/${name}")
        endforeach()
    endif()
endforeach()
vcpkg_copy_pdbs()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
# ozz installs a few loose text files (README, CHANGES...) into the root of the package.
file(GLOB loose_files "${CURRENT_PACKAGES_DIR}/*.md" "${CURRENT_PACKAGES_DIR}/debug/*.md")
if(loose_files)
    file(REMOVE ${loose_files})
endif()

# ozz ships no CMake package file; this one defines the targets ozz::base, ozz::animation
# and ozz::animation_offline.
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/ozz-animation-config.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")
