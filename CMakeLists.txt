cmake_minimum_required(VERSION 3.10)
project(boost_asio_tls CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
# REMOVED system in find package , cmake stubs remove from version 1.89 
find_package(Boost REQUIRED)
find_package(OpenSSL REQUIRED)


set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(tls_server server.cpp server.h)
add_custom_command(TARGET tls_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_SOURCE_DIR}/server.crt
    $<TARGET_FILE_DIR:tls_server>
    COMMENT "Copying certificate file to output directory"
)
add_custom_command(TARGET tls_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_SOURCE_DIR}/server.key
    $<TARGET_FILE_DIR:tls_server>
    COMMENT "Copying certificate key to output directory"
)
target_include_directories(tls_server PRIVATE /usr/local/include)
# This is the critical step for linking OpenSSL
target_link_libraries(tls_server PRIVATE
    Boost::boost
    OpenSSL::SSL
    OpenSSL::Crypto
)

