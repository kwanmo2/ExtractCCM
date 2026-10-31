cmake_minimum_required(VERSION 3.21)
project(ExtractCCM VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options(/W3 /utf-8 /permissive-)
    add_compile_definitions(_USE_MATH_DEFINES NOMINMAX)
endif()

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

# ---- core (Qt-independent) ----
add_library(ccmcore STATIC
    src/core/ColorMath.cpp
    src/core/Illuminant.cpp
    src/core/ChartSpec.cpp
    src/core/Targets.cpp
    src/core/ImageIO.cpp
    src/core/PatchSampler.cpp
    src/core/CcmSolver.cpp
    src/core/Report.cpp
)
target_include_directories(ccmcore PUBLIC src)
target_link_libraries(ccmcore PUBLIC ${OpenCV_LIBS})
target_include_directories(ccmcore PUBLIC ${OpenCV_INCLUDE_DIRS})

# ---- GUI ----
qt_add_executable(ExtractCCM WIN32
    src/main.cpp
    src/ui/MainWindow.cpp src/ui/MainWindow.h
    src/ui/ImageView.cpp src/ui/ImageView.h
    src/ui/ResultPanel.cpp src/ui/ResultPanel.h
    src/ui/RawImportDialog.cpp src/ui/RawImportDialog.h
    src/ui/Language.cpp src/ui/Language.h src/ui/Translations_ko.inc
)
target_link_libraries(ExtractCCM PRIVATE ccmcore Qt6::Widgets)

# Copy the Qt platform plugin (platforms/qwindows.dll) and all dependent DLLs next to the exe,
# so it runs without PATH changes (double-click, other PCs).
set(CCM_QT_PLUGIN_DIR "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}")
set(CCM_DLL_DIRS "${CCM_RUNTIME_DIR}" "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" "${OpenCV_DIR}/bin" "${OpenCV_DIR}/../bin")
string(REPLACE ";" "$<SEMICOLON>" CCM_DLL_DIRS_ARG "${CCM_DLL_DIRS}")
add_custom_command(TARGET ExtractCCM POST_BUILD
    COMMAND ${CMAKE_COMMAND} "-DEXE=$<TARGET_FILE:ExtractCCM>" "-DQT_PLUGIN_DIR=${CCM_QT_PLUGIN_DIR}"
            "-DSEARCH_DIRS=${CCM_DLL_DIRS_ARG}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/deploy.cmake"
    COMMENT "Deploying Qt plugins and runtime DLLs"
    VERBATIM)

# `cmake --install build/conda --prefix dist` produces a folder that can be copied to another PC
install(TARGETS ExtractCCM RUNTIME DESTINATION .)
install(CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\" \"-DEXE=\${CMAKE_INSTALL_PREFIX}/ExtractCCM.exe\"
    \"-DQT_PLUGIN_DIR=${CCM_QT_PLUGIN_DIR}\" \"-DSEARCH_DIRS=${CCM_DLL_DIRS}\"
    -P \"${CMAKE_CURRENT_SOURCE_DIR}/cmake/deploy.cmake\")")

# ---- tests ----
enable_testing()
add_executable(ccm_tests tests/test_core.cpp)
target_link_libraries(ccm_tests PRIVATE ccmcore)
add_test(NAME ccm_tests COMMAND ccm_tests)
if(CCM_RUNTIME_DIR)
    set_tests_properties(ccm_tests PROPERTIES ENVIRONMENT "PATH=${CCM_RUNTIME_DIR}\;$ENV{PATH}")
endif()
