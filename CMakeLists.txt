cmake_minimum_required(VERSION 3.24)
project(HeavyWeapon LANGUAGES C CXX)

# Native desktop port for Windows x64 (MSVC) and Linux x64, on SDL3 + legacy OpenGL.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Only 64-bit builds are supported (use an x64 toolchain)")
endif()

option(HW_PERF "Enable the perf.log profiler (src/core/Perf.h)" OFF)
set(HW_DATA_DIR "" CACHE PATH "Default game data directory, searched after the usual places")

set(HW_SDL3_VERSION       "3.4.20" CACHE STRING "SDL3 version to download")
set(HW_SDL3_IMAGE_VERSION "3.4.8"  CACHE STRING "SDL3_image version to download")
set(HW_SDL3_MIXER_VERSION "3.2.4"  CACHE STRING "SDL3_mixer version to download")
set(_gh https://github.com/libsdl-org)

set(SOURCES
    src/main.cpp
    src/platform/Vfs.cpp
    src/platform/AudioSystem.cpp
    src/platform/InputManager.cpp
    src/loader/XmlLoader.cpp
    src/render/TextureManager.cpp
    src/render/Renderer.cpp
    src/render/FontRenderer.cpp
    src/render/WorldRenderer.cpp
    src/core/GameEngine.cpp
    src/core/Perf.cpp
    src/game/Gfx.cpp
    src/game/Board.cpp
    src/game/Crafts.cpp
    src/game/Hazards.cpp
    src/game/Supplies.cpp
    src/game/Weapons.cpp
    src/game/Bosses.cpp
    3rdparty/tinyxml2/tinyxml2.cpp
)

find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------------------
# SDL3, SDL3_image, SDL3_mixer
# ---------------------------------------------------------------------------
if(WIN32)
    # Windows: the official prebuilt "devel VC" packages, downloaded once into the build
    # tree. Each ships a CMake config package; its DLLs are copied next to the exe.
    if(NOT MSVC)
        message(FATAL_ERROR "The Windows build uses MSVC (see tools/windows/build.cmd)")
    endif()
    set(_deps ${CMAKE_BINARY_DIR}/_deps)
    function(hw_fetch_vc var repo pkg ver)
        set(_dir ${_deps}/${pkg}-${ver})
        if(NOT EXISTS ${_dir}/cmake)
            set(_zip ${_deps}/${pkg}-devel-${ver}-VC.zip)
            message(STATUS "Downloading ${pkg} ${ver}")
            file(DOWNLOAD ${_gh}/${repo}/releases/download/release-${ver}/${pkg}-devel-${ver}-VC.zip
                 ${_zip} STATUS _st TLS_VERIFY ON)
            list(GET _st 0 _code)
            if(NOT _code EQUAL 0)
                file(REMOVE ${_zip})
                message(FATAL_ERROR "Failed to download ${pkg} ${ver}: ${_st}")
            endif()
            file(ARCHIVE_EXTRACT INPUT ${_zip} DESTINATION ${_deps})
            file(REMOVE ${_zip})
        endif()
        set(${var} ${_dir} PARENT_SCOPE)
    endfunction()
    hw_fetch_vc(_sdl3       SDL       SDL3       ${HW_SDL3_VERSION})
    hw_fetch_vc(_sdl3_image SDL_image SDL3_image ${HW_SDL3_IMAGE_VERSION})
    hw_fetch_vc(_sdl3_mixer SDL_mixer SDL3_mixer ${HW_SDL3_MIXER_VERSION})
    find_package(SDL3       REQUIRED CONFIG PATHS ${_sdl3}/cmake       NO_DEFAULT_PATH)
    find_package(SDL3_image REQUIRED CONFIG PATHS ${_sdl3_image}/cmake NO_DEFAULT_PATH)
    find_package(SDL3_mixer REQUIRED CONFIG PATHS ${_sdl3_mixer}/cmake NO_DEFAULT_PATH)
    # SDL3_image/SDL3_mixer load these codec DLLs at runtime (PNG, OGG/Opus, XMP, ...).
    file(GLOB _hw_runtime_dlls
        ${_sdl3}/lib/x64/*.dll
        ${_sdl3_image}/lib/x64/*.dll ${_sdl3_image}/lib/x64/optional/*.dll
        ${_sdl3_mixer}/lib/x64/*.dll ${_sdl3_mixer}/lib/x64/optional/*.dll)
else()
    # Linux: system packages when they are new enough, otherwise (or with
    # HW_BUNDLE_SDL=ON) build them from the release sources as static libraries.
    option(HW_BUNDLE_SDL "Build SDL3/SDL3_image/SDL3_mixer from source instead of using system packages" OFF)
    if(NOT HW_BUNDLE_SDL)
        find_package(SDL3 3.2 CONFIG QUIET)
        find_package(SDL3_image 3.2 CONFIG QUIET)
        find_package(SDL3_mixer 3.0 CONFIG QUIET)
        if(NOT (SDL3_FOUND AND SDL3_image_FOUND AND SDL3_mixer_FOUND))
            message(STATUS "System SDL3/SDL3_image/SDL3_mixer not all found: building them from source")
            set(HW_BUNDLE_SDL ON)
        endif()
    endif()
    if(HW_BUNDLE_SDL)
        include(FetchContent)
        set(BUILD_SHARED_LIBS OFF)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        # Only what the game loads: PNG/JPG through stb_image, OGG/WAV through the built-in
        # decoders. No extra system codec libraries are needed.
        set(SDLIMAGE_VENDORED OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_BACKEND_STB ON CACHE BOOL "" FORCE)
        set(SDLIMAGE_PNG_LIBPNG OFF CACHE BOOL "" FORCE)   # PNG via stb too: no libpng/zlib needed
        foreach(_f AVIF BMP GIF JXL LBM PCX PNM QOI SVG TGA TIF WEBP XCF XPM XV ANI HDR)
            set(SDLIMAGE_${_f} OFF CACHE BOOL "" FORCE)
        endforeach()
        set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_TESTS OFF CACHE BOOL "" FORCE)
        set(SDLMIXER_VENDORED OFF CACHE BOOL "" FORCE)
        foreach(_f FLAC GME MIDI MOD MP3 OPUS WAVPACK AIFF VOC AU)
            set(SDLMIXER_${_f} OFF CACHE BOOL "" FORCE)
        endforeach()
        set(SDLMIXER_VORBIS_STB ON CACHE BOOL "" FORCE)
        set(SDLMIXER_WAVE ON CACHE BOOL "" FORCE)
        set(SDLMIXER_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(SDLMIXER_TESTS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(SDL3
            URL ${_gh}/SDL/releases/download/release-${HW_SDL3_VERSION}/SDL3-${HW_SDL3_VERSION}.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_Declare(SDL3_image
            URL ${_gh}/SDL_image/releases/download/release-${HW_SDL3_IMAGE_VERSION}/SDL3_image-${HW_SDL3_IMAGE_VERSION}.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_Declare(SDL3_mixer
            URL ${_gh}/SDL_mixer/releases/download/release-${HW_SDL3_MIXER_VERSION}/SDL3_mixer-${HW_SDL3_MIXER_VERSION}.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(SDL3 SDL3_image SDL3_mixer)
    endif()
endif()

# ---------------------------------------------------------------------------
# Game
# ---------------------------------------------------------------------------
add_executable(heavyweapon WIN32 ${SOURCES})
target_include_directories(heavyweapon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/tinyxml2)
target_link_libraries(heavyweapon PRIVATE
    SDL3_image::SDL3_image SDL3_mixer::SDL3_mixer SDL3::SDL3 OpenGL::GL)
if(HW_PERF)
    target_compile_definitions(heavyweapon PRIVATE HW_PERF)
endif()
if(HW_DATA_DIR)
    file(TO_CMAKE_PATH "${HW_DATA_DIR}" _hw_data_dir)
    target_compile_definitions(heavyweapon PRIVATE "HW_DATA_DIR=\"${_hw_data_dir}\"")
endif()

if(WIN32)
    target_compile_definitions(heavyweapon PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
    target_compile_options(heavyweapon PRIVATE /W3 /utf-8 /MP)
    set_property(TARGET heavyweapon PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:heavyweapon>)
    add_custom_command(TARGET heavyweapon POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_hw_runtime_dlls} $<TARGET_FILE_DIR:heavyweapon>
        VERBATIM)
    install(TARGETS heavyweapon RUNTIME DESTINATION .)
    install(FILES ${_hw_runtime_dlls} DESTINATION .)
else()
    target_compile_definitions(heavyweapon PRIVATE GL_GLEXT_PROTOTYPES)
    target_compile_options(heavyweapon PRIVATE -Wall -Wextra)
    install(TARGETS heavyweapon RUNTIME DESTINATION bin)
endif()
