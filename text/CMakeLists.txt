cmake_minimum_required(VERSION 3.16)

project(text VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_executable(text
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    prompterwidget.cpp
    prompterwidget.h
)

target_link_libraries(text PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

add_custom_command(TARGET text POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/text.json
        $<TARGET_FILE_DIR:text>/text.json
)

set_target_properties(text PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS text
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES text.json
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)
