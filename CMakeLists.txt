cmake_minimum_required(VERSION 3.16)
project(NeoQuake LANGUAGES CXX C)

# ===== Verbose / defaults =====
option(NEOQUAKE_VERBOSE "Print compile/link commands" ON)
if (NEOQUAKE_VERBOSE)
  set(CMAKE_VERBOSE_MAKEFILE ON CACHE BOOL "" FORCE) # Makefiles/MinGW
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer Release by default
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# MSVC niceties
if(MSVC)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# ===== GLFW discovery =====
option(NEOQUAKE_FETCH_GLFW "Fetch GLFW with FetchContent if not found" ON)
find_package(glfw3 CONFIG QUIET)
if(NOT glfw3_FOUND AND NEOQUAKE_FETCH_GLFW)
  include(FetchContent)
  set(FETCHCONTENT_QUIET FALSE)
  FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8
  )
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(glfw)
endif()

# ===== Which executables to build =====
option(BUILD_VIEWER "Build NeoQuakeViewer (BSP viewer)" ON)
option(BUILD_GAME   "Build NeoQuakeGame (playable free-fly)" ON)

# ===== Platform OpenGL lib target =====
set(NEOQUAKE_OPENGL_LIB "")
if (WIN32)
  set(NEOQUAKE_OPENGL_LIB opengl32)
elseif(APPLE)
  find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
  set(NEOQUAKE_OPENGL_LIB ${OPENGL_FRAMEWORK})
else()
  find_package(OpenGL REQUIRED)
  set(NEOQUAKE_OPENGL_LIB OpenGL::GL)
endif()

# ===== Core engine library =====
add_library(neoquake_core STATIC
  # --- BSP / data ---
  src/BSP_Load.cpp
  src/BSP_Convert.cpp
  src/BSP_Palette.cpp
  src/BSP_Entities.cpp
  src/BSP_Lightmaps.cpp

  # --- Camera split ---
  src/Camera_Proj.cpp
  src/Camera_View.cpp
  src/Camera_Move.cpp

  # --- Renderer split ---
  src/Renderer_Upload.cpp
  src/Renderer_Draw.cpp

  # --- Texture split ---
  src/Texture_CreatePtr.cpp
  src/Texture_CreateVec.cpp

  # --- Shader split (stub + dynamic) ---
  src/Shader_Load.cpp
  src/Shader_Build.cpp

  # --- Input/game layer ---
  src/Input.cpp
  src/Game.cpp                  # remove if not present
)

target_include_directories(neoquake_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(neoquake_core PUBLIC -DNEOQUAKE_VERSION=\"0.1.0\")

# Link GLFW + OpenGL (and gdi32 on Windows to quiet some toolchains)
target_link_libraries(neoquake_core PUBLIC glfw ${NEOQUAKE_OPENGL_LIB})
if (WIN32)
  target_link_libraries(neoquake_core PUBLIC gdi32)
endif()

# ===== Executables =====
# Helper to attach shader/asset copy to a target
function(copy_runtime_assets tgt)
  add_custom_command(TARGET ${tgt} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${tgt}>/shaders"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${tgt}>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/basic.vert
            "$<TARGET_FILE_DIR:${tgt}>/shaders/basic.vert"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/basic.frag
            "$<TARGET_FILE_DIR:${tgt}>/shaders/basic.frag"
  )
endfunction()

if (BUILD_VIEWER)
  add_executable(NeoQuakeViewer src/main_viewer.cpp)  # your viewer main
  target_link_libraries(NeoQuakeViewer PRIVATE neoquake_core)
  target_include_directories(NeoQuakeViewer PRIVATE ${CMAKE_SOURCE_DIR}/src)
  copy_runtime_assets(NeoQuakeViewer)
endif()

if (BUILD_GAME)
  add_executable(NeoQuakeGame src/main_game.cpp)      # your game main
  target_link_libraries(NeoQuakeGame PRIVATE neoquake_core)
  target_include_directories(NeoQuakeGame PRIVATE ${CMAKE_SOURCE_DIR}/src)
  copy_runtime_assets(NeoQuakeGame)
endif()

# ===== Clean-all (nuke build dir) =====
add_custom_target(clean-all
  COMMAND ${CMAKE_COMMAND} -E echo "Purging: ${CMAKE_BINARY_DIR}"
  COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}"
  COMMENT "Remove the entire build directory (you'll need to re-configure)"
)
