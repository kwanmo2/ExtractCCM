# Makes an executable self-contained on Windows:
#   1. copies the Qt plugins it needs (platforms/qwindows.dll, styles, imageformats) next to the exe
#   2. copies every non-system DLL the exe and those plugins depend on next to the exe
# Usage: cmake -DEXE=<path> -DQT_PLUGIN_DIR=<dir> -DSEARCH_DIRS=<dir;dir> -P deploy.cmake
cmake_minimum_required(VERSION 3.21)

get_filename_component(EXE_DIR "${EXE}" DIRECTORY)

set(PLUGIN_DLLS "")
foreach(sub platforms styles imageformats)
    file(GLOB dlls "${QT_PLUGIN_DIR}/${sub}/*.dll")
    list(FILTER dlls EXCLUDE REGEX "d\\.dll$")  # debug variants
    if(sub STREQUAL "platforms")
        list(FILTER dlls INCLUDE REGEX "q(windows|offscreen|minimal)\\.dll$")
    endif()
    foreach(dll IN LISTS dlls)
        file(COPY "${dll}" DESTINATION "${EXE_DIR}/${sub}")
        get_filename_component(name "${dll}" NAME)
        list(APPEND PLUGIN_DLLS "${EXE_DIR}/${sub}/${name}")
    endforeach()
endforeach()
if(NOT EXISTS "${EXE_DIR}/platforms/qwindows.dll")
    message(WARNING "deploy: qwindows.dll not found under ${QT_PLUGIN_DIR}/platforms")
endif()

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${EXE}"
    LIBRARIES ${PLUGIN_DLLS}
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    # On a rebuild the DLLs already sit next to the exe as well as in the search dirs
    CONFLICTING_DEPENDENCIES_PREFIX conflict
    DIRECTORIES ${SEARCH_DIRS}
    PRE_EXCLUDE_REGEXES "^api-ms-.*" "^ext-ms-.*"
    POST_EXCLUDE_REGEXES ".*[/\\\\][Ss]ystem32[/\\\\].*" ".*[/\\\\][Ww]indows[/\\\\].*")

foreach(dll IN LISTS resolved)
    get_filename_component(name "${dll}" NAME)
    if(NOT EXISTS "${EXE_DIR}/${name}")
        file(COPY "${dll}" DESTINATION "${EXE_DIR}")
    endif()
endforeach()

# conda's liblapack/libcblas forward to Intel MKL, which loads its kernels with LoadLibrary at run
# time - invisible to the dependency scan. Copy the runtime set when MKL is the BLAS provider.
if(EXISTS "${EXE_DIR}/liblapack.dll" OR EXISTS "${EXE_DIR}/libcblas.dll")
    foreach(dir IN LISTS SEARCH_DIRS)
        file(GLOB mkl_rt "${dir}/mkl_rt*.dll")
        if(mkl_rt)
            file(GLOB mkl_dlls "${dir}/mkl_rt*.dll" "${dir}/mkl_core*.dll" "${dir}/mkl_intel_thread*.dll"
                 "${dir}/mkl_def*.dll" "${dir}/mkl_mc*.dll" "${dir}/mkl_avx*.dll" "${dir}/mkl_vml_*.dll"
                 "${dir}/mkl_msg.dll" "${dir}/libiomp5md.dll")
            foreach(dll IN LISTS mkl_dlls)
                get_filename_component(name "${dll}" NAME)
                if(NOT EXISTS "${EXE_DIR}/${name}")
                    file(COPY "${dll}" DESTINATION "${EXE_DIR}")
                endif()
            endforeach()
            break()
        endif()
    endforeach()

    # With the OpenBLAS variant (libblas=*=*openblas) they forward their exports to openblas.dll.
    # Forwarded exports are not imports, so the dependency scan misses it too.
    foreach(fwd liblapack.dll libcblas.dll)
        if(EXISTS "${EXE_DIR}/${fwd}")
            file(STRINGS "${EXE_DIR}/${fwd}" uses_openblas REGEX "openblas\\." LIMIT_COUNT 1)
            if(uses_openblas AND NOT EXISTS "${EXE_DIR}/openblas.dll")
                foreach(dir IN LISTS SEARCH_DIRS)
                    if(EXISTS "${dir}/openblas.dll")
                        file(COPY "${dir}/openblas.dll" DESTINATION "${EXE_DIR}")
                        break()
                    endif()
                endforeach()
            endif()
        endif()
    endforeach()
endif()

if(unresolved)
    message(STATUS "deploy: unresolved (expected for OS-provided DLLs): ${unresolved}")
endif()
