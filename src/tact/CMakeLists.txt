# TactCppLib from the 3rdparty/TactCpp sources, with its own CMakeLists.txt left out: that one fetches
# cpr and libcurl for the HTTP code in CDN.cpp and ListFile.cpp. The two of that name beside this one
# stand in for them, so nothing here fetches, compiles or links an HTTP client.
set(TACT_LIB_DIR ${PROJECT_SOURCE_DIR}/3rdparty/TactCpp/TactCppLib)

add_library(TactCppLib STATIC
		${TACT_LIB_DIR}/utils/KeyService.cpp
		${TACT_LIB_DIR}/utils/Jenkins96.cpp
		${TACT_LIB_DIR}/utils/SalsaDecrypt.cpp
		${TACT_LIB_DIR}/utils/TactConfigParser.cpp
		${TACT_LIB_DIR}/BLTE.cpp
		${TACT_LIB_DIR}/Settings.cpp
		${TACT_LIB_DIR}/RootInstance.cpp
		${TACT_LIB_DIR}/FileCache.cpp
		${TACT_LIB_DIR}/MemoryMappedFile.cpp
		${TACT_LIB_DIR}/EncodingInstance.cpp
		${TACT_LIB_DIR}/IndexInstance.cpp
		${TACT_LIB_DIR}/InstallInstance.cpp
		${TACT_LIB_DIR}/BuildInstance.cpp
		${TACT_LIB_DIR}/GroupIndex.cpp
		CDN.cpp
		ListFile.cpp
)

target_include_directories(TactCppLib PRIVATE ${TACT_LIB_DIR})
target_link_libraries(TactCppLib zlib)

set_property(TARGET TactCppLib PROPERTY CXX_STANDARD 23)
set_property(TARGET TactCppLib PROPERTY CXX_STANDARD_REQUIRED ON)
