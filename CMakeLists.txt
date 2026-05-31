cmake_minimum_required(VERSION 3.16)
project(wisdom-weasel-linux VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 选项
option(BUILD_DAEMON "Build Wisdom-Weasel daemon" ON)
option(BUILD_FCITX5_PLUGIN "Build fcitx5 plugin" ON)
option(BUILD_CORE_ONLY "Build only the core library" OFF)

# 核心库
add_subdirectory(wisdom-core)

# 守护进程
if(BUILD_DAEMON AND NOT BUILD_CORE_ONLY)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(RIME REQUIRED rime)

    add_executable(wisdom-daemon
        wisdom-daemon/src/daemon.cpp
        wisdom-daemon/src/daemon.h
    )

    target_include_directories(wisdom-daemon
        PRIVATE
            wisdom-daemon/src
            ${RIME_INCLUDE_DIRS}
    )

    target_link_libraries(wisdom-daemon
        PRIVATE
            wisdom-core
            ${RIME_LIBRARIES}
            pthread
    )

    install(TARGETS wisdom-daemon
        RUNTIME DESTINATION bin
    )
endif()

# fcitx5 插件
if(BUILD_FCITX5_PLUGIN AND NOT BUILD_CORE_ONLY)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FCITX5 REQUIRED Fcitx5Core)

    add_library(wisdom-plugin MODULE
        fcitx5-addon/src/wisdom-weasel.cpp
    )

    # fcitx5 5.1.17 需要 C++20
    set_target_properties(wisdom-plugin PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )

    target_include_directories(wisdom-plugin
        PRIVATE
            ${FCITX5_INCLUDE_DIRS}
    )

    target_link_libraries(wisdom-plugin
        PRIVATE
            ${FCITX5_LIBRARIES}
    )

    # 生成插件配置文件
    configure_file(
        fcitx5-addon/wisdom-weasel.conf.in
        wisdom-weasel.conf
        @ONLY
    )

    install(TARGETS wisdom-plugin
        LIBRARY DESTINATION lib/fcitx5
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/wisdom-weasel.conf
        DESTINATION share/fcitx5/addon
    )
endif()

# 安装 README
install(FILES README.md DESTINATION share/doc/wisdom-weasel)
