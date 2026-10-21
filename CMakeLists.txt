cmake_minimum_required(VERSION 3.21)

project(gimp-vtf
	VERSION 2.0.0
	DESCRIPTION "Valve Texture Format plug-in for GIMP 3"
	LANGUAGES C CXX
)

include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

set(VTFLIB_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/VTFLib" CACHE PATH
	"Path to the VTFLib++ source tree (https://github.com/coreyzd/VTFLib)")

option(GIMP_VTF_SHARED_VTFLIB
	"Link VTFLib++ as a shared library (installed next to the plug-in) instead of statically"
	OFF)

set(GIMP_VTF_PLUGIN_DIR "${CMAKE_INSTALL_LIBDIR}/gimp/3.0/plug-ins" CACHE PATH
	"Plug-ins folder to install into. Relative paths are relative to CMAKE_INSTALL_PREFIX.")

if(MINGW)
	option(GIMP_VTF_STATIC_RUNTIME "Link libgcc/libstdc++ statically (MinGW)" ON)
endif()

if(MSVC)
	message(WARNING
		"GIMP's libraries are built with MinGW and don't ship MSVC import libraries. "
		"Build the plug-in from an MSYS2 UCRT64 or CLANG64 shell instead (see README.md).")
endif()

# ---------------------------------------------------------------------------
# GIMP 3
# ---------------------------------------------------------------------------

find_package(PkgConfig REQUIRED)
pkg_check_modules(GIMP REQUIRED IMPORTED_TARGET gimp-3.0>=3.0 gimpui-3.0>=3.0)

message(STATUS "Building against GIMP ${GIMP_gimp-3.0_VERSION}")

# ---------------------------------------------------------------------------
# VTFLib++
# ---------------------------------------------------------------------------

if(NOT EXISTS "${VTFLIB_SOURCE_DIR}/VTFLib/CMakeLists.txt")
	message(FATAL_ERROR
		"VTFLib++ was not found in ${VTFLIB_SOURCE_DIR}.\n"
		"Run: git submodule update --init --recursive\n"
		"or pass -DVTFLIB_SOURCE_DIR=<path to a VTFLib++ checkout>.")
endif()

# Only the library is needed. EXCLUDE_FROM_ALL means that third-party code
# VTFLib++ doesn't link against (DevIL, OpenEXR...) is configured but never built.
set(VTFLIB_BUILD_VTFCMD  OFF CACHE BOOL "" FORCE)
set(VTFLIB_BUILD_VTFEDIT OFF CACHE BOOL "" FORCE)
set(VTFLIB_BUILD_FUZZER  OFF CACHE BOOL "" FORCE)
if(NOT GIMP_VTF_SHARED_VTFLIB)
	set(VTFLIB_BUILD_STATIC ON)
endif()

# libjpeg-turbo (pulled in by VTFLib++'s third-party folder) replaces a default
# install prefix with /opt/<project>; keep ours.
set(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT OFF)
add_subdirectory("${VTFLIB_SOURCE_DIR}" vtflib EXCLUDE_FROM_ALL)

# Workarounds for building VTFLib++'s Compressonator with MinGW, in case the
# VTFLib++ checkout doesn't have them yet (they are harmless if it does):
#  * cmp_core uses SSE4.1 intrinsics whenever _M_X64 is defined, which MinGW
#    also defines; MSVC accepts them without a flag, GCC/Clang need -msse4.1.
#  * bc6h_encode.cpp uses std::isinf without including <cmath> (libc++).
if(MINGW AND TARGET CMP_Core AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
	target_compile_options(CMP_Core PUBLIC -msse4.1)
endif()
if(NOT MSVC AND TARGET CMP_Compressonator)
	set_source_files_properties(
		"${VTFLIB_SOURCE_DIR}/thirdparty/compressonator/cmp_compressonatorlib/bc6h/bc6h_encode.cpp"
		TARGET_DIRECTORY CMP_Compressonator
		PROPERTIES COMPILE_OPTIONS "-include;cmath")
endif()

if(GIMP_VTF_SHARED_VTFLIB)
	set(GIMP_VTF_VTFLIB_TARGET vtflib)
else()
	set(GIMP_VTF_VTFLIB_TARGET vtflib_static)
endif()

# ---------------------------------------------------------------------------
# The plug-in
# ---------------------------------------------------------------------------

# GIMP 3 expects each plug-in in a folder of the same name:
#   plug-ins/file-vtf/file-vtf[.exe]
# The build tree mirrors that, so build/file-vtf can be copied as-is.
set(GIMP_VTF_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/file-vtf")

add_executable(file-vtf
	gimp-vtf/file-vtf.c
	gimp-vtf/file-vtf.h
	gimp-vtf/file-vtf-load.c
	gimp-vtf/file-vtf-save.c
	gimp-vtf/file-vtf-settings.c
	gimp-vtf/file-vtf-ui.c
)

if(WIN32)
	target_sources(file-vtf PRIVATE gimp-vtf/file-vtf.rc)
	set_target_properties(file-vtf PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

set_target_properties(file-vtf PROPERTIES
	C_STANDARD 11
	RUNTIME_OUTPUT_DIRECTORY "${GIMP_VTF_OUTPUT_DIR}"
)

foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
	set_target_properties(file-vtf PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY_${config} "${GIMP_VTF_OUTPUT_DIR}")
endforeach()

target_compile_definitions(file-vtf PRIVATE
	GETTEXT_PACKAGE="file-vtf"
	GIMP_VTF_VERSION="${PROJECT_VERSION}"
	GIMP_DISABLE_DEPRECATED
)

# VTFLib++'s own include path is relative to the top of whichever project
# includes it, so add the public header's folder explicitly.
target_include_directories(file-vtf PRIVATE "${VTFLIB_SOURCE_DIR}/lib")

target_link_libraries(file-vtf PRIVATE
	PkgConfig::GIMP
	${GIMP_VTF_VTFLIB_TARGET}
)

if(UNIX)
	target_link_libraries(file-vtf PRIVATE m)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	target_compile_options(file-vtf PRIVATE -Wall -Wno-unused-parameter)
endif()

if(GIMP_VTF_SHARED_VTFLIB)
	# Put VTFLib++ next to the plug-in and make the plug-in look for it there
	foreach(config "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
		set_target_properties(vtflib PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY${config} "${GIMP_VTF_OUTPUT_DIR}"
			LIBRARY_OUTPUT_DIRECTORY${config} "${GIMP_VTF_OUTPUT_DIR}")
	endforeach()

	if(APPLE)
		set_target_properties(file-vtf PROPERTIES
			BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
	elseif(UNIX)
		set_target_properties(file-vtf PROPERTIES
			BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
	endif()
endif()

if(MINGW AND GIMP_VTF_STATIC_RUNTIME)
	target_link_options(file-vtf PRIVATE -static-libgcc -static-libstdc++)
	if(GIMP_VTF_SHARED_VTFLIB)
		target_link_options(vtflib PRIVATE -static-libgcc -static-libstdc++)
	endif()
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

install(TARGETS file-vtf
	RUNTIME DESTINATION "${GIMP_VTF_PLUGIN_DIR}/file-vtf")

if(GIMP_VTF_SHARED_VTFLIB)
	install(TARGETS vtflib
		RUNTIME DESTINATION "${GIMP_VTF_PLUGIN_DIR}/file-vtf"
		LIBRARY DESTINATION "${GIMP_VTF_PLUGIN_DIR}/file-vtf")
endif()
