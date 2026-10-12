cmake_minimum_required(VERSION 3.10)

project(FlyoverViewer)
include(ExternalProject)

enable_language( C CXX ASM )

set(CMAKE_VERBOSE_MAKEFILE ON)

if(POLICY CMP0077)
	cmake_policy(SET CMP0077 NEW)
endif()
if(COMMAND cmake_policy)
	cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)
cmake_policy(SET CMP0079 NEW)

if (WIN32 AND MSVC)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /DWIN64")
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /DWIN64")
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /DWIN64")
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /DWIN64")
	endif()

	add_definitions(-DWIN32_LEAN_AND_MEAN)
 endif()
message(CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE})

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	link_libraries(stdc++fs)
endif()


if(WIN32)
	link_libraries(wsock32 ws2_32 -ldbghelp winmm)
endif()

#ZLib library
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")
add_subdirectory(${CMAKE_SOURCE_DIR}/3rdparty/zlib EXCLUDE_FROM_ALL)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/3rdparty/zlib)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")

if (TRUE)
	set(ZLIB_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/3rdparty/zlib ${CMAKE_BINARY_DIR}/3rdparty/zlib)
    message("CMAKE_BINARY_DIR = ${CMAKE_BINARY_DIR}")
	message("CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")
endif()


add_definitions(-DSQLITE_ENABLE_FTS5)
include_directories(${CMAKE_SOURCE_DIR}/3rdparty/SQLiteCpp/sqlite3)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "")
add_subdirectory(${CMAKE_SOURCE_DIR}/3rdparty/glfw EXCLUDE_FROM_ALL)

#c++ 17 FS implementation
add_subdirectory(${CMAKE_SOURCE_DIR}/3rdparty/filesystem_impl EXCLUDE_FROM_ALL)

include(FetchContent)

if (WIN32)
	set(WITH_LIBTOMCRYPT true)
endif()

add_subdirectory(src/tact)

add_subdirectory(wowViewerLib)


set(SOURCE_FILES
		src/persistance/CascRequestProcessor.cpp
		src/persistance/CascRequestProcessor.h
		src/persistance/StorageError.h

		src/ui/imguiLib/imgui.cpp
		src/ui/imguiLib/imgui_draw.cpp
		src/ui/imguiLib/imgui_widgets.cpp
		src/ui/imguiLib/imgui_tables.cpp
		src/ui/imguiLib/imguiImpl/imgui_impl_glfw.cpp
		src/ui/FrontendUI.cpp src/ui/FrontendUI.h
		src/ui/StorageNotifications.h
		src/database/CSqliteDB.cpp src/database/CSqliteDB.h
		src/database/CEmptySqliteDB.cpp
		src/database/CEmptySqliteDB.h
		src/database/buildInfoParser/buildDefinition.h
		src/database/buildInfoParser/buildInfoParser.cpp
		src/database/buildInfoParser/buildInfoParser.h
		src/ui/renderer/uiScene/FrontendUIRenderer.cpp
		src/ui/renderer/uiScene/FrontendUIRenderer.h
		src/ui/renderer/uiScene/FrontendUIRendererFactory.cpp
		src/ui/renderer/uiScene/FrontendUIRendererFactory.h
		src/ui/renderer/uiScene/ImGUIPlan.cpp
		src/ui/renderer/uiScene/ImGUIPlan.h
		src/ui/renderer/uiScene/IFrontendUIBufferCreate.h
		src/ui/renderer/uiScene/materials/UIMaterial.h
		src/ui/childWindow/sceneWindow/SceneWindow.cpp
		src/ui/childWindow/sceneWindow/SceneWindow.h
		src/ui/childWindow/cascStorageDialog/CascStorageDialog.cpp
		src/ui/childWindow/cascStorageDialog/CascStorageDialog.h
		src/ui/childWindow/cascStorageDialog/CascStorageEntry.h
		src/flyover/FlyoverMode.cpp
		src/flyover/FlyoverMode.h
		src/flyover/FlyoverConfig.cpp
		src/flyover/FlyoverConfig.h
		src/flyover/CrashStack.cpp
		src/flyover/CrashStack.h
)

set(SOURCE_FILES_VULKAN
		src/ui/renderer/uiScene/vulkan/FrontendUIRenderForwardVLK.cpp
		src/ui/renderer/uiScene/vulkan/FrontendUIRenderForwardVLK.h)

#Build the executiable
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package( Threads REQUIRED )

add_subdirectory(${PROJECT_SOURCE_DIR}/3rdparty/SQLiteCpp EXCLUDE_FROM_ALL)
if (NOT Vulkan_FOUND)
	set(SOURCE_FILES_VULKAN "")
endif()

add_library(WoWFrontendLib STATIC ${SOURCE_FILES} ${SOURCE_FILES_VULKAN})
add_executable(flyover-viewer src/main.cpp)
target_link_libraries(flyover-viewer WoWFrontendLib)
if (MSVC)
	# A PDB with public symbols beside the exe, which the crash handler reads for its stack; the optimisations /DEBUG turns off are kept on.
	target_link_options(flyover-viewer PRIVATE /DEBUG /OPT:REF /OPT:ICF)
endif()

if (Vulkan_FOUND)
	add_definitions(-DLINK_VULKAN)
endif()


message(ccp WoWViewerLib_INCLUDE_DIRS = ${WoWViewerLib_INCLUDE_DIRS})
target_include_directories(WoWFrontendLib PUBLIC ${WoWViewerLib_INCLUDE_DIRS})
target_include_directories(WoWFrontendLib PUBLIC ${PROJECT_SOURCE_DIR}/WoWViewerLib/3rdparty/mathfu/include)
target_include_directories(WoWFrontendLib PUBLIC ${PROJECT_SOURCE_DIR}/WoWViewerLib/3rdparty/mathfu/include)

#target_compile_options(WoWFrontendLib PRIVATE "-static")
target_compile_definitions(WoWFrontendLib PUBLIC -DIMGUI_USER_CONFIG=<imguiCustomConfig.h>)
target_include_directories(WoWFrontendLib PUBLIC src/ui/imguiLib)

add_dependencies(WoWFrontendLib WoWViewerLib)

#TODO: platform dependant!!
add_dependencies(WoWFrontendLib glfw)
if (NOT MSVC)
	check_cxx_compiler_flag(-std=c++17 HAVE_FLAG_STD_CXX17)
	if(HAVE_FLAG_STD_CXX17)
		# Have -std=c++17, use it
		message("WOWLIB HAVE_FLAG_STD_CXX17 is supported")
		#    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -std=c++17" )
		#    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -std=c++17" )
		set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -std=c++17" )
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -std=c++17" )
	else()
		check_cxx_compiler_flag(-std=c++1z HAVE_FLAG_STD_CXX1Z)
		if(HAVE_FLAG_STD_CXX1Z)
			#        set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -std=c++1z" )
			#        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -std=c++1z" )
			set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -std=c++1z")
			set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -std=c++1z")
		else()
			message(ERROR "No supported flags")
		endif()
	endif()
endif()

if (MSVC)
	include(CheckCXXCompilerFlag)
	CHECK_CXX_COMPILER_FLAG("/std:c++20" _cpp_20_flag_supported)
	message("MSVC Is on")
	if (_cpp_20_flag_supported)
		message("/std:c++20 is supported")
		#target_compile_options(WoWFrontendLib "/std:c++20")
		set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /std:c++20 /permissive-")
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /std:c++20 /permissive-")
		set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /std:c++20 /permissive-")
		#target_compile_options(WoWFrontendLib /std:c++20)
	endif()
endif()

#target_link_libraries(WoWFrontendLib zlibstatic)

target_link_libraries(WoWFrontendLib SQLiteCpp sqlite3)
if (NOT MSVC)
    find_library(DL_EXISTS dl)
    message("DL_EXISTS = ${DL_EXISTS}")
    if (NOT DL_EXISTS STREQUAL "DL_EXISTS-NOTFOUND")
        message("Linking against DL")
        target_link_libraries(WoWFrontendLib dl)
        target_compile_options(WoWFrontendLib PRIVATE "-fstack-protector")
    else()
        find_library(SSP_EXISTS ssp)
        message("SSP_EXISTS = ${SSP_EXISTS}")
        if (NOT SSP_EXISTS STREQUAL "SSP_EXISTS-NOTFOUND")
            message("Linking against SSP")
            target_link_libraries(WoWFrontendLib ssp)
            target_compile_options(WoWFrontendLib PRIVATE "-fstack-protector")
        endif()

        message( "Neither DL nor SSP was found" )
    endif()
endif(NOT MSVC)

target_link_libraries(WoWFrontendLib WoWViewerLib)

target_link_libraries(WoWFrontendLib Threads::Threads)
target_link_libraries(WoWFrontendLib glfw)

target_link_libraries(WoWFrontendLib TactCppLib)

#target_link_options(WoWFrontendLib PUBLIC "SHELL: -llibg")

install(TARGETS flyover-viewer
		EXPORT ${PROJECT_NAME}Targets
		LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
		ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
		COMPONENT libraries)

INSTALL(FILES ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION bin COMPONENT Libraries)

if(WIN32)
	# zlib's own install rules are skipped by EXCLUDE_FROM_ALL.
	install(FILES "$<TARGET_FILE:zlib>" DESTINATION bin COMPONENT libraries)
	# The signed FSR 3.1 DLL the upscaler loads by name; without it beside the executable the scene is stretched instead.
	install(FILES "${PROJECT_SOURCE_DIR}/3rdparty/FidelityFX-SDK/PrebuiltSignedDLL/amd_fidelityfx_vk.dll" DESTINATION bin COMPONENT libraries)
	# Also beside the built executable, so a run from the build tree gets FSR too.
	add_custom_command(TARGET flyover-viewer POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${PROJECT_SOURCE_DIR}/3rdparty/FidelityFX-SDK/PrebuiltSignedDLL/amd_fidelityfx_vk.dll"
			"$<TARGET_FILE_DIR:flyover-viewer>")
endif()

if( MINGW )
	message( STATUS "Installing system-libraries: MinGW DLLs." )
	get_filename_component( Mingw_Path ${CMAKE_CXX_COMPILER} PATH )
	set( CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
			${Mingw_Path}/libgcc_s_seh-1.dll
			${Mingw_Path}/libstdc++-6.dll
			${Mingw_Path}/libwinpthread-1.dll
	)
endif( MINGW )
include( InstallRequiredSystemLibraries )

# Actually install it when make install is called.
if( CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS )
	install( PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION bin COMPONENT System )
endif( CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS )
