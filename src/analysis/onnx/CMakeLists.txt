set(LADIO_ONNXRUNTIME_ROOT "" CACHE PATH "Unpacked ONNX Runtime C/C++ SDK")
find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
    HINTS "${LADIO_ONNXRUNTIME_ROOT}/include" "${LADIO_ONNXRUNTIME_ROOT}/headers"
          "${LADIO_ONNXRUNTIME_ROOT}/include/onnxruntime/core/session")
find_library(ONNXRUNTIME_LIBRARY NAMES onnxruntime libonnxruntime
    HINTS "${LADIO_ONNXRUNTIME_ROOT}/lib" "${LADIO_ONNXRUNTIME_ROOT}/lib64"
          "${LADIO_ONNXRUNTIME_ROOT}/jni/${ANDROID_ABI}")
if(NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIBRARY)
    message(FATAL_ERROR "ONNX SDK not found. Set LADIO_ONNXRUNTIME_ROOT or ONNXRUNTIME_INCLUDE_DIR and ONNXRUNTIME_LIBRARY.")
endif()
add_library(ladio_ort UNKNOWN IMPORTED)
set_target_properties(ladio_ort PROPERTIES IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}")
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WITH_LSR_BINDINGS OFF CACHE BOOL "" FORCE)
set(WITH_OPENMP OFF CACHE BOOL "" FORCE)
# Optional LGPL component is a separate replaceable dynamic library.
set(_saved_shared ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS ON)
add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/soxr ${CMAKE_CURRENT_BINARY_DIR}/soxr EXCLUDE_FROM_ALL)
set(BUILD_SHARED_LIBS ${_saved_shared})
if(APPLE)
    set_target_properties(soxr PROPERTIES MACOSX_RPATH ON INSTALL_NAME_DIR "@rpath")
endif()
add_library(ladio_analysis SHARED models.cpp analysis.cpp descriptors.cpp material.cpp pair.cpp api.cpp)
add_library(Ladio::Analysis ALIAS ladio_analysis)
set_target_properties(ladio_analysis PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION 1
    EXPORT_NAME Analysis POSITION_INDEPENDENT_CODE ON WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_include_directories(ladio_analysis PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE . ${PROJECT_SOURCE_DIR}/src/core ${PROJECT_SOURCE_DIR}/src/analysis
    ${PROJECT_SOURCE_DIR}/third_party ${PROJECT_SOURCE_DIR}/third_party/soxr/src)
target_link_libraries(ladio_analysis PRIVATE Ladio::Core
    $<BUILD_INTERFACE:ladio_ort> $<BUILD_INTERFACE:soxr>)
if(ANDROID)
    target_link_options(ladio_analysis PRIVATE -Wl,-z,max-page-size=16384)
    target_link_options(soxr PRIVATE -Wl,-z,max-page-size=16384)
endif()
if(LADIO_BUILD_TOOLS)
    add_executable(ladio_analyze ${PROJECT_SOURCE_DIR}/tools/analyze.cpp)
    target_include_directories(ladio_analyze PRIVATE ${PROJECT_SOURCE_DIR}/include)
    target_link_libraries(ladio_analyze PRIVATE ladio_analysis Ladio::Core)
    install(TARGETS ladio_analyze RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(TARGETS ladio_analysis EXPORT LadioTargets LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
set_target_properties(soxr PROPERTIES PUBLIC_HEADER "${PROJECT_SOURCE_DIR}/third_party/soxr/src/soxr.h")
install(TARGETS soxr EXPORT LadioTargets LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/soxr
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
# The application supplies ONNX Runtime and its notices for its chosen platform.
