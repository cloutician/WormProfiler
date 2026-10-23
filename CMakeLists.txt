cmake_minimum_required(VERSION 3.19)
project(WormProfiler LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Concurrent Svg)
find_package(OpenCV 4.9 REQUIRED)

qt_standard_project_setup()
set(CMAKE_AUTORCC ON)

set(REVIEW_SOURCES
    src/ui/CropReviewWorkspace.cpp
    src/ui/CropReviewWorkspace.h
    src/io/CropReviewSession.cpp
    src/io/CropReviewSession.h
    src/io/PreprocessingProfile.cpp
    src/io/PreprocessingProfile.h
)

set(APP_SOURCES
    src/ui/mainwindow.cpp
    src/ui/mainwindow.h
    src/ui/mainwindow.ui
    src/ui/ImageViewer.cpp
    src/ui/ImageViewer.h
    src/ui/ImageNavigation.cpp
    src/ui/ImageNavigation.h
    src/ui/DesktopStyle.cpp
    src/ui/DesktopStyle.h
    src/ui/DesktopIcons.cpp
    src/ui/DesktopIcons.h
    src/ui/StartupSplash.cpp
    src/ui/StartupSplash.h
    src/ui/WormProfileWorkspace.cpp
    src/ui/WormProfileWorkspace.h
    resources/desktop.qrc
    src/io/ImageLoader.cpp
    src/io/ImageLoader.h
    src/io/WormProfile.cpp
    src/io/WormProfile.h
    src/preprocessing/PreprocessingPipeline.h
    src/preprocessing/PreprocessingPipeline.cpp
    src/preprocessing/WormCropper.h
    src/preprocessing/WormCropper.cpp
    src/preprocessing/ProcessingJobs.cpp
    src/preprocessing/ProcessingJobs.h
)

qt_add_executable(WormProfiler WIN32 MACOSX_BUNDLE main.cpp ${REVIEW_SOURCES} ${APP_SOURCES})

if(WIN32)
    enable_language(RC)
    configure_file(resources/windows/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/wormprofiler.rc" @ONLY)
    target_sources(WormProfiler PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/wormprofiler.rc")
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/wormprofiler.rc" PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/branding/wormprofiler.ico")
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    qt_add_executable(WormProfilerTests tests/ReviewWorkflowTests.cpp ${REVIEW_SOURCES} ${APP_SOURCES})
    target_include_directories(WormProfilerTests PRIVATE src src/io src/ui src/preprocessing ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(WormProfilerTests PRIVATE Qt::Widgets Qt::Test Qt::Concurrent Qt::Svg ${OpenCV_LIBS}
        $<$<PLATFORM_ID:Windows>:dwmapi>)
    add_test(NAME ReviewWorkflow COMMAND WormProfilerTests -o -,txt)
    set_tests_properties(ReviewWorkflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45)
endif()

target_link_libraries(WormProfiler
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Concurrent
        Qt::Svg
        ${OpenCV_LIBS}
        $<$<PLATFORM_ID:Windows>:dwmapi>
)

target_include_directories(WormProfiler
    PRIVATE
        ${OpenCV_INCLUDE_DIRS}
        src
        src/ui
        src/io
        src/preprocessing
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
install(FILES resources/icons/lucide/LICENSE DESTINATION licenses/lucide)
