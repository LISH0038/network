cmake_minimum_required(VERSION 3.9)
project(network)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_C_STANDARD 11)
set(OPENSSL_ROOT_DIR /usr/local/Cellar/openssl@1.1/1.1.1n)
set(Libuv_DIR /usr/local/Cellar/libuv/1.44.1/)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/salticidae/cmake/Modules/" "${CMAKE_CURRENT_PROJECT_DIR}/cmake")
include_directories(
#        salticidae/
#        salticidae/include/
#        salticidae/include/salticidae
        /usr/local/Cellar/libuv/1.44.1/include
        /usr/local/Cellar/openssl@1.1/1.1.1n/include
        /usr/local/include/

)
#find_package(Libuv 1.10.0 REQUIRED)
find_package(OpenSSL 1.1.0 REQUIRED)

#find_package(OpenSSL)
#find_package(Libuv)
link_libraries(ssl)
link_libraries(crypto)
#link_libraries(salticidae)
link_directories(/usr/local/lib)
link_directories(/usr/local/include/salticidae)
link_directories(/usr/local/Cellar/libuv/1.44.1/lib)
link_directories(/usr/local/Cellar/openssl@1.1/1.1.1n/lib)

#add_subdirectory(salticidae)
#add_executable(network main.cpp ${SOURCE_FILES})
add_executable(network main.cpp p2p.cpp test.cpp)
target_link_libraries(network salticidae uv crypto ssl pthread)
#target_link_libraries(salticidae)

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -W -Wall -Wextra -pedantic -Wsuggest-override")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -W -Wall -Wextra -pedantic -Wsuggest-override")
