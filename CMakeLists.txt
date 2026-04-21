cmake_minimum_required(VERSION 3.19)
project(WormProfiler LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)
find_package(OpenCV REQUIRED)

qt_standard_project_setup()

qt_add_executable(WormProfiler
    WIN32 MACOSX_BUNDLE
    main.cpp
    src/ui/mainwindow.cpp
    src/ui/mainwindow.h
    src/ui/mainwindow.ui
    src/io/ImageLoader.cpp
    src/io/ImageLoader.h
)

target_link_libraries(WormProfiler
    PRIVATE
        Qt::Core
        Qt::Widgets
        ${OpenCV_LIBS}
)

target_include_directories(WormProfiler
    PRIVATE
        ${OpenCV_INCLUDE_DIRS}
        src
        src/ui
        src/io
    )

install(TARGETS WormProfiler
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET WormProfiler
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
