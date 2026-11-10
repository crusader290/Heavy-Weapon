cmake_minimum_required(VERSION 3.20)
project(HeavyWeaponDeluxe LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

option(HW_PERF "Enable the perf.log profiler (src/core/Perf.h)" OFF)

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
)

# Optional default game-data directory compiled into desktop builds (searched last).
set(HW_DATA_DIR "" CACHE PATH "Default game data directory for desktop builds")

# ---------------------------------------------------------------------------
# Windows x64 build (MSVC): SDL2, SDL2_image and SDL2_mixer come from the official
# libsdl-org "devel VC" release zips, fetched at configure time. Their DLLs are copied
# next to heavyweapon.exe. See CMakePresets.json (cmake --preset windows-x64).
# ---------------------------------------------------------------------------
if(WIN32 AND NOT DEFINED ENV{VITASDK} AND NOT DEFINED VITASDK)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "Only 64-bit Windows builds are supported (use an x64 toolchain)")
    endif()
    set(HW_SDL2_VERSION       "2.32.10" CACHE STRING "SDL2 version")
    set(HW_SDL2_IMAGE_VERSION "2.8.12"  CACHE STRING "SDL2_image version")
    set(HW_SDL2_MIXER_VERSION "2.8.2"   CACHE STRING "SDL2_mixer version")
    set(_gh https://github.com/libsdl-org)
    set(_deps ${CMAKE_BINARY_DIR}/_deps)

    # Download and unpack a prebuilt devel zip once; sets <var> to its top-level folder.
    function(hw_fetch_sdl var repo pkg ver)
        set(_dir ${_deps}/${pkg}-${ver})
        if(NOT EXISTS ${_dir}/include)
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
    hw_fetch_sdl(sdl2_vc_SOURCE_DIR       SDL       SDL2       ${HW_SDL2_VERSION})
    hw_fetch_sdl(sdl2_image_vc_SOURCE_DIR SDL_image SDL2_image ${HW_SDL2_IMAGE_VERSION})
    hw_fetch_sdl(sdl2_mixer_vc_SOURCE_DIR SDL_mixer SDL2_mixer ${HW_SDL2_MIXER_VERSION})

    # The code includes <SDL2/SDL.h>; the VC packages ship flat include/ folders, so
    # stage every header under an SDL2/ prefix.
    set(_sdl_inc ${CMAKE_BINARY_DIR}/sdl2-include)
    foreach(_src ${sdl2_vc_SOURCE_DIR} ${sdl2_image_vc_SOURCE_DIR} ${sdl2_mixer_vc_SOURCE_DIR})
        file(COPY ${_src}/include/ DESTINATION ${_sdl_inc}/SDL2)
    endforeach()

    function(hw_import_sdl name dir)
        add_library(${name} SHARED IMPORTED)
        set_target_properties(${name} PROPERTIES
            IMPORTED_IMPLIB   ${dir}/lib/x64/${name}.lib
            IMPORTED_LOCATION ${dir}/lib/x64/${name}.dll
            INTERFACE_INCLUDE_DIRECTORIES "${_sdl_inc};${_sdl_inc}/SDL2")
    endfunction()
    hw_import_sdl(SDL2       ${sdl2_vc_SOURCE_DIR})
    hw_import_sdl(SDL2_image ${sdl2_image_vc_SOURCE_DIR})
    hw_import_sdl(SDL2_mixer ${sdl2_mixer_vc_SOURCE_DIR})
    add_library(SDL2main STATIC IMPORTED)
    set_target_properties(SDL2main PROPERTIES IMPORTED_LOCATION ${sdl2_vc_SOURCE_DIR}/lib/x64/SDL2main.lib)
    find_package(OpenGL REQUIRED)

    add_executable(heavyweapon WIN32 ${SOURCES} 3rdparty/tinyxml2/tinyxml2.cpp)
    target_include_directories(heavyweapon PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/tinyxml2)
    target_compile_definitions(heavyweapon PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
    if(MSVC)
        target_compile_options(heavyweapon PRIVATE /W3 /utf-8 /MP)
        set_property(TARGET heavyweapon PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:heavyweapon>)
    endif()
    if(HW_DATA_DIR)
        file(TO_CMAKE_PATH "${HW_DATA_DIR}" _hw_data_dir)
        target_compile_definitions(heavyweapon PRIVATE "HW_DATA_DIR=\"${_hw_data_dir}\"")
    endif()
    if(HW_PERF)
        target_compile_definitions(heavyweapon PRIVATE HW_PERF)
    endif()
    target_link_libraries(heavyweapon PRIVATE SDL2main SDL2 SDL2_image SDL2_mixer OpenGL::GL)

    # Runtime DLLs (SDL2*.dll plus SDL2_image/SDL2_mixer optional codec DLLs) next to the exe.
    file(GLOB _hw_dlls
        ${sdl2_vc_SOURCE_DIR}/lib/x64/*.dll
        ${sdl2_image_vc_SOURCE_DIR}/lib/x64/*.dll
        ${sdl2_image_vc_SOURCE_DIR}/lib/x64/optional/*.dll
        ${sdl2_mixer_vc_SOURCE_DIR}/lib/x64/*.dll
        ${sdl2_mixer_vc_SOURCE_DIR}/lib/x64/optional/*.dll)
    add_custom_command(TARGET heavyweapon POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_hw_dlls} $<TARGET_FILE_DIR:heavyweapon>
        VERBATIM)
    install(TARGETS heavyweapon RUNTIME DESTINATION .)
    install(FILES ${_hw_dlls} DESTINATION .)
    return()
endif()

# ---------------------------------------------------------------------------
# Desktop build (Linux): same game code on SDL2 + legacy OpenGL, for fast iteration
# and screenshots. See tools/desktop/hw.sh.
# ---------------------------------------------------------------------------
if(NOT DEFINED ENV{VITASDK} AND NOT DEFINED VITASDK)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2 SDL2_image SDL2_mixer)
    find_package(OpenGL REQUIRED)

    add_executable(heavyweapon ${SOURCES} 3rdparty/tinyxml2/tinyxml2.cpp)
    target_include_directories(heavyweapon PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/tinyxml2)
    target_compile_definitions(heavyweapon PRIVATE GL_GLEXT_PROTOTYPES)
    target_compile_options(heavyweapon PRIVATE -Wall -Wextra)
    if(HW_PERF)
        target_compile_definitions(heavyweapon PRIVATE HW_PERF)
    endif()
    target_link_libraries(heavyweapon PRIVATE PkgConfig::SDL2 OpenGL::GL)
    return()
endif()

# ---------------------------------------------------------------------------
# PS Vita build
# ---------------------------------------------------------------------------
if(NOT DEFINED VITASDK)
    set(VITASDK $ENV{VITASDK})
endif()
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Heavy Weapon Deluxe")
set(VITA_TITLEID "HWDL00001")
set(VITA_VERSION "01.00")

add_executable(heavyweapon ${SOURCES})

target_compile_definitions(heavyweapon PRIVATE
    __vita__
)
if(HW_PERF)
    target_compile_definitions(heavyweapon PRIVATE HW_PERF)
endif()

target_link_libraries(heavyweapon
    -Wl,--start-group
    SDL2_image
    SDL2_mixer
    SDL2
    vitaGL
    vitashark
    mathneon
    SceShaccCgExt
    SceShaccCg_stub
    taihen_stub
    SceKernelDmacMgr_stub
    vorbisfile
    vorbis
    ogg
    modplug
    opusfile
    opus
    mpg123
    xmp
    png
    jpeg
    webpdemux
    webp
    tinyxml2
    z
    m
    pthread
    SceLibKernel_stub
    SceDisplay_stub
    SceGxm_stub
    SceCtrl_stub
    SceTouch_stub
    SceAudio_stub
    SceAudioIn_stub
    SceMotion_stub
    SceHid_stub
    SceIme_stub
    ScePower_stub
    SceAppMgr_stub
    SceSysmodule_stub
    SceCommonDialog_stub
    -Wl,--end-group
)

# The signed SELF must not share the .velf name: vita.cmake writes the intermediate
# VELF to heavyweapon.velf, and a parallel build could then pack the unsigned file.
vita_create_self(heavyweapon.self heavyweapon UNSAFE)

vita_create_vpk(HeavyWeaponDeluxe.vpk ${VITA_TITLEID} heavyweapon.self
    NAME ${VITA_APP_NAME}
    FILE sce_sys/icon0.png sce_sys/icon0.png
    FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
    FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
    FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)
