# Copyright 2020 NVIDIA Corporation
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.31)
# Construct the project name PROJNAME from the directory name
get_filename_component(PROJNAME ${CMAKE_CURRENT_LIST_DIR} NAME)
set(PROJNAME "vk_mini_path_tracer_${PROJNAME}")
project(${PROJNAME})


find_path(BASE_DIRECTORY
        NAMES nvpro_core/cmake/setup.cmake
        PATHS ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../..
        REQUIRED
        DOC "Directory containing nvpro_core"
)
if(EXISTS ${BASE_DIRECTORY}/nvpro_core/cmake/setup.cmake)
include(${BASE_DIRECTORY}/nvpro_core/cmake/setup.cmake)
else()
message(FATAL_ERROR "could not find base directory, please set BASE_DIRECTORY to folder containing nvpro_core")
endif()
#####################################################################################
_add_package_VulkanSDK()
_add_nvpro_core_lib()
_add_project_definitions(${PROJNAME})

#####################################################################################
# Source files for this project
#
file(GLOB SOURCE_FILES *.cpp *.hpp *.inl *.h *.c)
list(APPEND SOURCE_FILES path_tracer_window.cpp)
list(APPEND SOURCE_FILES D:/nvpro_core/third_party/tinyobjloader/obj_loader.cpp)


#####################################################################################
# For _edit, we add a check to make sure readers create a shaders directory
# and place shaders inside that, instead of placing them in the main directory.
# It's possible to miss creating this directory, and the effect will be that
# shaders never appear to be compiled.
# Instead of being a fatal error, we automatically fix things by copying files
# to the right place when this happens. This only occurs for .glsl files, since
# the first chapter that introduces shaders only uses .glsl files, and copying
# .h files could cause problems if readers experiment with adding libraries.
file(GLOB _GLSL_WRONG_LOCATION_FILES "*.glsl")
if(_GLSL_WRONG_LOCATION_FILES)
  message(WARNING "It looks like some .glsl files were placed in the top level of _edit, rather than making a 'shaders' subdirectory and placing the shaders inside that! This script will move them to the shaders directory.")
  file(MAKE_DIRECTORY "shaders")
endif()
foreach(_FILE ${_GLSL_WRONG_LOCATION_FILES})
  get_filename_component(_FILE_NAME ${_FILE} NAME)
  set(_NEW_FILE "${CMAKE_CURRENT_LIST_DIR}/shaders/${_FILE_NAME}")
  if(EXISTS "${_NEW_FILE}")
    message(WARNING "Tried to move ${_FILE} to ${_NEW_FILE}, but ${_NEW_FILE} already existed! In other words, there are two copies of a shader, one in the shaders directory (which will be compiled), and one outside of that. One may wish to compare the files to determine which is the correct one.")
  else()
    file(RENAME "${_FILE}" "${_NEW_FILE}")
  endif()
endforeach()

#####################################################################################
# GLSL to SPIR-V custom build
#
# more than one file can be given: _compile_GLSL("GLSL_mesh.vert;GLSL_mesh.frag" "GLSL_mesh.spv" GLSL_SOURCES)
# the SPIR-V validator is fine as long as files are for different pipeline stages (entry points still need to be main())
#_compile_GLSL(<source(s)> <target spv> <LIST where files are appended>)
SET(VULKAN_TARGET_ENV vulkan1.2)

UNSET(GLSL_SOURCES)
UNSET(SPV_OUTPUT)
file(GLOB_RECURSE GLSL_HEADER_FILES "shaders/*.h")
file(GLOB_RECURSE GLSL_SOURCE_FILES "shaders/*.glsl")
foreach(GLSL ${GLSL_SOURCE_FILES})
    get_filename_component(FILE_NAME ${GLSL} NAME)
    _compile_GLSL(${GLSL} "shaders/${FILE_NAME}.spv" GLSL_SOURCES SPV_OUTPUT)
endforeach(GLSL)

list(APPEND GLSL_SOURCES ${GLSL_HEADER_FILES})
source_group("Shader Files" FILES ${GLSL_SOURCES})

#####################################################################################
# Executable
#
add_executable(${PROJNAME} ${SOURCE_FILES} ${COMMON_SOURCE_FILES} ${GLSL_SOURCES}
        ../../nvpro_core/third_party/imgui/backends/imgui_impl_glfw.cpp
        ../../nvpro_core/third_party/imgui/backends/imgui_impl_vulkan.cpp
        ../../nvpro_core/third_party/imgui/imgui.cpp
        ../../nvpro_core/imgui/imgui_helper.cpp
        ../../nvpro_core/imgui/imgui_camera_widget.cpp)

#####################################################################################
# Source code group
#
source_group("Source Files" FILES ${SOURCE_FILES})

#####################################################################################
# Linkage
#
target_link_libraries(${PROJNAME} ${PLATFORM_LIBRARIES} nvpro_core)

foreach(DEBUGLIB ${LIBRARIES_DEBUG})
  target_link_libraries(${PROJNAME} debug ${DEBUGLIB})
endforeach(DEBUGLIB)

foreach(RELEASELIB ${LIBRARIES_OPTIMIZED})
  target_link_libraries(${PROJNAME} optimized ${RELEASELIB})
endforeach(RELEASELIB)

#####################################################################################
# copies binaries that need to be put next to the exe files (ZLib, etc.)
#
_finalize_target( ${PROJNAME} )

install(FILES ${SPV_OUTPUT} CONFIGURATIONS Release DESTINATION "bin_${ARCH}/${PROJNAME}/shaders")
install(FILES ${SPV_OUTPUT} CONFIGURATIONS Debug DESTINATION "bin_${ARCH}_debug/${PROJNAME}/shaders")
install(DIRECTORY "../scenes" CONFIGURATIONS Release DESTINATION "bin_${ARCH}/${PROJNAME}")
install(DIRECTORY "../scenes" CONFIGURATIONS Debug DESTINATION "bin_${ARCH}_debug/${PROJNAME}")