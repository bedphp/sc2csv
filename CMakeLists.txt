cmake_minimum_required(VERSION 3.24)

project(sc2csv VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

if(MSVC)
    add_compile_options(/utf-8 /permissive- /Zc:__cplusplus)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ---------------------------------------------------------------------------
# Dependencies: Dear ImGui (docking branch) and GLFW
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND  OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_X11      OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)

# CupertinoUi uses imgui_internal.h, so the docking branch is required.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(glfw imgui)

# ---------------------------------------------------------------------------
# Dear ImGui static library (core + GLFW/OpenGL3 backends)
# ---------------------------------------------------------------------------
add_library(sc_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
)
target_include_directories(sc_imgui PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
    "${imgui_SOURCE_DIR}/misc/cpp"
)
target_compile_definitions(sc_imgui PUBLIC
    IMGUI_USE_WCHAR32
    IMGUI_DEFINE_MATH_OPERATORS
)
target_link_libraries(sc_imgui PUBLIC glfw)
if(WIN32)
    target_link_libraries(sc_imgui PUBLIC opengl32)
elseif(APPLE)
    target_link_libraries(sc_imgui PUBLIC "-framework OpenGL")
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(sc_imgui PUBLIC OpenGL::GL)
endif()

# ---------------------------------------------------------------------------
# CupertinoUi (vendored, MIT) - macOS-style ImGui widgets
#
# The framework lives in third_party/cupertino and is built as a static
# library here. Upstream README/docs are kept next to the sources.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE CUPERTINO_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cupertino/app/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cupertino/controls/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cupertino/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cupertino/debug/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cupertino/layout/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cupertino/overlays/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cupertino/views/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cupertino/window/*.cpp"
)
list(APPEND CUPERTINO_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cupertino/Cupertino.cpp"
)

add_library(cupertino_ui STATIC ${CUPERTINO_SOURCES})
target_include_directories(cupertino_ui PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cupertino"
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cupertino/third_party/stb"
)
target_compile_definitions(cupertino_ui PUBLIC
    IMGUI_DEFINE_MATH_OPERATORS
    IMGUI_USE_WCHAR32
)
target_link_libraries(cupertino_ui PUBLIC sc_imgui)
if(WIN32)
    target_link_libraries(cupertino_ui PRIVATE dwmapi)
endif()

# ---------------------------------------------------------------------------
# CSV containers: raw LZMA1 with a 9-byte Supercell header
#
# LzmaDec is the public-domain LZMA SDK decoder, vendored under
# third_party/lzma. It only needs 7zTypes.h/Compiler.h/CpuArch.h beside it.
# ---------------------------------------------------------------------------
# Z7_ST builds the encoder single-threaded, so the multithreaded match finder
# (LzFindMt/MtCoder/Threads) and its 7zWindows.h are not needed.
add_library(sc_lzma STATIC
    third_party/lzma/LzmaDec.c
    third_party/lzma/LzmaEnc.c
    third_party/lzma/LzFind.c
    third_party/lzma/LzmaLib.c
    third_party/lzma/Alloc.c
    third_party/lzma/CpuArch.c
)
target_include_directories(sc_lzma PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lzma"
)
target_compile_definitions(sc_lzma PRIVATE Z7_ST)

add_library(sc_csv STATIC
    src/csv_codec.cpp
    src/csv_project.cpp
)
target_include_directories(sc_csv PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(sc_csv PUBLIC sc_lzma)

# ---------------------------------------------------------------------------
# Translations: src/i18n/*.csv -> generated/i18n_data.inc
#
# The generator reads $lang/$name/$order and embeds every table verbatim; src/i18n.cpp parses the CSV once at
# startup. Listing the CSV files as dependencies means editing a translation rebuilds the .inc, and the glob
# with CONFIGURE_DEPENDS picks up a newly added language on the next build.
# ---------------------------------------------------------------------------
set(I18N_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/i18n")
file(GLOB I18N_CSV_FILES CONFIGURE_DEPENDS "${I18N_SOURCE_DIR}/*.csv")
set(I18N_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(I18N_INC "${I18N_GENERATED_DIR}/i18n_data.inc")

# icon/ico.png is the only icon source. The build derives everything from it:
#   * src/generated/AboutIcon.h      -- the 128 px PNG the About panel draws, baked in at compile time
#   * build/generated/icon/sc2csv.ico -- the 16-256 px Windows icon, embedded in the executable by rc.exe
#   * build/generated/icon/sc2csv.rc  -- the resource script naming that .ico
# Regenerating whenever the PNG changes keeps the About panel and the executable from drifting apart.
find_package(Python3 COMPONENTS Interpreter QUIET)
set(ICON_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/icon/ico.png")
set(ICON_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/src/generated/AboutIcon.h")
set(ICON_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/icon")
set(ICON_ICO "${ICON_GENERATED_DIR}/sc2csv.ico")
set(ICON_RC "${ICON_GENERATED_DIR}/sc2csv.rc")
set(ICON_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateIcon.py")
if(Python3_Interpreter_FOUND)
    add_custom_command(
        OUTPUT "${ICON_HEADER}" "${ICON_ICO}" "${ICON_RC}"
        COMMAND Python3::Interpreter "${ICON_SCRIPT}"
                "${ICON_SOURCE}" "${ICON_HEADER}" "${ICON_GENERATED_DIR}"
        DEPENDS "${ICON_SOURCE}" "${ICON_SCRIPT}"
        COMMENT "Generating the application icon from icon/ico.png"
        VERBATIM
    )
    add_custom_target(sc2csv_icon DEPENDS "${ICON_HEADER}" "${ICON_ICO}" "${ICON_RC}")
else()
    message(WARNING "Python 3 not found: the icon is not regenerated and the executable gets no icon. "
                    "The committed src/generated/AboutIcon.h is still used.")
endif()

add_custom_command(
    OUTPUT "${I18N_INC}"
    COMMAND "${CMAKE_COMMAND}"
            "-DI18N_SOURCE_DIR=${I18N_SOURCE_DIR}"
            "-DI18N_OUTPUT=${I18N_INC}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateI18n.cmake"
    DEPENDS ${I18N_CSV_FILES} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateI18n.cmake"
    COMMENT "Generating i18n_data.inc"
    VERBATIM
)
add_custom_target(sc2csv_i18n DEPENDS "${I18N_INC}")

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
add_executable(sc2csv
    src/main.cpp
    src/i18n.cpp
)
add_dependencies(sc2csv sc2csv_i18n)
if(TARGET sc2csv_icon)
    add_dependencies(sc2csv sc2csv_icon)
endif()
target_sources(sc2csv PRIVATE "${I18N_INC}")
set_source_files_properties("${I18N_INC}" PROPERTIES HEADER_FILE_ONLY TRUE)
# i18n.cpp includes the generated file, so its object must wait for the generator even on a fresh build.
set_source_files_properties(src/i18n.cpp PROPERTIES OBJECT_DEPENDS "${I18N_INC}")
# The About icon is read at compile time, so main.cpp's object waits for the bake the same way.
set_source_files_properties(src/main.cpp PROPERTIES OBJECT_DEPENDS "${ICON_HEADER}")
target_include_directories(sc2csv PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${I18N_GENERATED_DIR}"
)
target_link_libraries(sc2csv PRIVATE cupertino_ui sc_csv)
if(WIN32)
    target_link_libraries(sc2csv PRIVATE ole32 shell32 uuid)

    # Explorer, the taskbar and Alt+Tab read the icon from the executable's resource section, which rc.exe builds from
    # the .ico generated beside the .rc. main.cpp loads the same resource for the GLFW window icon, so no .ico file has
    # to ship next to the exe.
    if(CMAKE_RC_COMPILER AND TARGET sc2csv_icon)
        set_source_files_properties("${ICON_RC}" PROPERTIES GENERATED TRUE OBJECT_DEPENDS "${ICON_ICO}")
        target_sources(sc2csv PRIVATE "${ICON_RC}")
    elseif(CMAKE_RC_COMPILER)
        message(STATUS "No icon resource: Python 3 is needed to build icon/ico.png into the executable.")
    else()
        message(STATUS "No Windows resource compiler: the executable gets no icon.")
    endif()

    # Debug keeps the console: std::fprintf(stderr, ...) diagnostics and an attached debugger have somewhere to write.
    # Release is a window application, so launching it from Explorer leaves no console behind the window.
    set_target_properties(sc2csv PROPERTIES
        WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>
    )
    if(MSVC)
        target_link_options(sc2csv PRIVATE
            "$<$<NOT:$<CONFIG:Debug>>:/SUBSYSTEM:WINDOWS>"
            "$<$<NOT:$<CONFIG:Debug>>:/ENTRY:mainCRTStartup>"
        )
    endif()
endif()
