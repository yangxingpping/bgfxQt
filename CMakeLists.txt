cmake_minimum_required(VERSION 3.16)

project(bgfxQt VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
find_package(Qt6 REQUIRED COMPONENTS OpenGLWidgets)
find_package(bgfx CONFIG REQUIRED)
find_package(manifold CONFIG REQUIRED)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        BgfxWindow.h
        BgfxWindow.cpp
        NavigationCube.h
        NavigationCube.cpp
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(bgfxQt
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET bgfxQt APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(bgfxQt SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(bgfxQt
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(bgfxQt PRIVATE Qt6::OpenGLWidgets PRIVATE Qt${QT_VERSION_MAJOR}::Widgets PRIVATE bgfx::bx bgfx::bgfx bgfx::bimg bgfx::bimg_decode PRIVATE manifold::manifold)

# --- bgfx shaders (shaderc) -------------------------------------------------
# Derive the vcpkg/install prefix from the resolved bgfx config location so
# shaderc and the shader include headers (bgfx_shader.sh) match the bgfx
# runtime exactly. Both can be overridden via -DBGFX_SHADERC / -DBGFX_SHADER_INCLUDE_DIR.
get_filename_component(_bgfx_share_dir "${bgfx_DIR}" DIRECTORY)
get_filename_component(_bgfx_prefix "${_bgfx_share_dir}" DIRECTORY)

set(BGFX_SHADERC "${_bgfx_prefix}/tools/bgfx/shaderc${CMAKE_EXECUTABLE_SUFFIX}"
    CACHE FILEPATH "Path to bgfx shaderc tool")
set(BGFX_SHADER_INCLUDE_DIR "${_bgfx_prefix}/include/bgfx"
    CACHE PATH "Directory containing bgfx_shader.sh")

if(NOT EXISTS "${BGFX_SHADERC}")
    message(FATAL_ERROR "shaderc not found: ${BGFX_SHADERC}. Install bgfx tools (vcpkg install bgfx[tools]) or set BGFX_SHADERC.")
endif()

set(_shader_gen_dir "${CMAKE_BINARY_DIR}/generated/shaders")

function(bgfx_compile_shader outvar src stage platform profile array_name)
    set(out "${_shader_gen_dir}/${array_name}.bin.h")
    add_custom_command(
        OUTPUT "${out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_shader_gen_dir}"
        COMMAND "${BGFX_SHADERC}"
                -f "${src}"
                -o "${out}"
                --bin2c ${array_name}
                --platform ${platform}
                -p ${profile}
                --type ${stage}
                -i "${BGFX_SHADER_INCLUDE_DIR}"
        DEPENDS "${src}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/varying.def.sc"
        COMMENT "Compiling bgfx shader ${array_name}"
        VERBATIM
    )
    set(${outvar} "${out}" PARENT_SCOPE)
endfunction()

set(_shader_headers "")
foreach(_shader cube textured)
    foreach(_stage vs fs)
        if(_stage STREQUAL "vs")
            set(_type vertex)
        else()
            set(_type fragment)
        endif()

        # Direct3D 11 / 12 share the same s_5_0 bytecode.
        bgfx_compile_shader(_h
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${_stage}_${_shader}.sc"
            ${_type} windows s_5_0 "${_stage}_${_shader}_dx11")
        list(APPEND _shader_headers "${_h}")

        # Vulkan (SPIR-V).
        bgfx_compile_shader(_h
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${_stage}_${_shader}.sc"
            ${_type} linux spirv "${_stage}_${_shader}_vk")
        list(APPEND _shader_headers "${_h}")
    endforeach()
endforeach()

add_custom_target(bgfx_shaders ALL DEPENDS ${_shader_headers})
add_dependencies(bgfxQt bgfx_shaders)
target_include_directories(bgfxQt PRIVATE "${_shader_gen_dir}")

# --- manifold runtime DLLs (classic vcpkg mode does not auto-deploy) -------
# manifold is a shared library; copy its DLL and its oneTBB dependency next to
# the executable so the app runs without vcpkg/bin on PATH. Use the config-
# aware TARGET_FILE so Debug builds pick up the debug manifold.dll (otherwise
# the release DLL's STL layout corrupts std::vector return values).
get_filename_component(_manifold_prefix "${manifold_DIR}/../.." ABSOLUTE)
add_custom_command(TARGET bgfxQt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:manifold::manifold>
        "$<IF:$<CONFIG:Debug>,${_manifold_prefix}/debug/bin/tbb12_debug.dll,${_manifold_prefix}/bin/tbb12.dll>"
        $<TARGET_FILE_DIR:bgfxQt>
    COMMENT "Copying manifold runtime DLLs"
    VERBATIM
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.bgfxQt)
endif()
set_target_properties(bgfxQt PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS bgfxQt
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(bgfxQt)
endif()
