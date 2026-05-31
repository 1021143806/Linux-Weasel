cmake_minimum_required(VERSION 3.16)
project(wisdom-core VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖
find_package(CURL REQUIRED)

# 头文件
set(WISDOM_CORE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 源文件
set(WISDOM_CORE_SOURCES
    src/http_client.cpp
    src/openai_provider.cpp
    src/context_history.cpp
    src/memory_compressor.cpp
)

# 创建库
add_library(wisdom-core STATIC ${WISDOM_CORE_SOURCES})

# 包含目录
target_include_directories(wisdom-core
    PUBLIC
        ${WISDOM_CORE_INCLUDE_DIR}
    PRIVATE
        ${CURL_INCLUDE_DIRS}
)

# 链接库
target_link_libraries(wisdom-core
    PUBLIC
        ${CURL_LIBRARIES}
)

# 安装
install(TARGETS wisdom-core
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY include/wisdom-core
    DESTINATION include
)
