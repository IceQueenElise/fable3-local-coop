cmake_minimum_required(VERSION 3.20)
project(Fable3Coop VERSION 1.0.0 LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
	message(FATAL_ERROR "Fable3.exe is 32-bit: configure with -A Win32")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The game loads xinput1_3.dll from its own folder first, so the mod ships under that name.
add_library(xinput1_3 SHARED
	src/bindings.cpp
	src/custommenu.cpp
	src/dllmain.cpp
	src/fixes.cpp
	src/gui.cpp
	src/guiscreen.cpp
	src/log.cpp
	src/luaprobe.cpp
	src/proxy.cpp
	src/input.cpp
	src/join.cpp
	src/netprobe.cpp
	src/picker.cpp
	src/routing.cpp
	src/saveio.cpp
	src/saves.cpp
	src/xinput1_3.def
)
target_compile_definitions(xinput1_3 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(xinput1_3 PRIVATE /W4 /permissive-)
target_link_libraries(xinput1_3 PRIVATE shell32 ole32)

set(FABLE3_DIR "F:/Fit Games/Fable 3" CACHE PATH "Fable III install folder for the deploy target")
add_custom_target(deploy
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:xinput1_3> "${FABLE3_DIR}/xinput1_3.dll"
	DEPENDS xinput1_3
	COMMENT "Copying xinput1_3.dll into ${FABLE3_DIR}"
)

# Release zip: only what goes into the game folder, plus the install steps.
set(RELEASE_STAGE "${CMAKE_BINARY_DIR}/package")  # not "release": Windows paths ignore case (build/Release)
set(RELEASE_ZIP "${CMAKE_BINARY_DIR}/Fable3Coop-v${PROJECT_VERSION}.zip")
add_custom_target(release
	COMMAND ${CMAKE_COMMAND} -E rm -rf "${RELEASE_STAGE}" "${RELEASE_ZIP}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${RELEASE_STAGE}"
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:xinput1_3> "${RELEASE_STAGE}/xinput1_3.dll"
	COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/config/fable3coop.ini" "${RELEASE_STAGE}/fable3coop.ini"
	COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/docs/INSTALL.md" "${RELEASE_STAGE}/INSTALL.md"
	COMMAND ${CMAKE_COMMAND} -E chdir "${RELEASE_STAGE}" ${CMAKE_COMMAND} -E tar cf "${RELEASE_ZIP}" --format=zip xinput1_3.dll fable3coop.ini INSTALL.md
	DEPENDS xinput1_3
	COMMENT "Packaging ${RELEASE_ZIP}"
)
