cmake_minimum_required(VERSION 3.10)

# Set the project name and language
project(hll LANGUAGES CXX)

# Add the source files
set(SOURCES
    interface.cpp
    analysis.cpp
    parser.cpp
    lexer.cpp
    rex.cpp
    json.cpp
    interpreter.cpp
    api.cpp
    unix_socket_client.cpp
    validate.cpp
)

# Find libcurl
find_package(CURL REQUIRED)

# Create the executable
add_executable(hll ${SOURCES})

# Link libcurl to your executable
target_link_libraries(hll PRIVATE CURL::libcurl)
