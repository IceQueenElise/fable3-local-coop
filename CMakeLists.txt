cmake_minimum_required(VERSION 3.20)
project(Fable3Coop LANGUAGES CXX)

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
	COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:xinput1_3> "${FABLE3_DIR}/xinput1_3.dll"	DEPENDS xinput1_3
	COMMENT "Copying xinput1_3.dll into ${FABLE3_DIR}"
)
