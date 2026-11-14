add_library(libyate SHARED
    TelEngine.cpp
    ObjList.cpp
    HashList.cpp
    Mutex.cpp
    Thread.cpp
    Socket.cpp
    Resolver.cpp
    String.cpp
    DataBlock.cpp
    NamedList.cpp
    URI.cpp
    Mime.cpp
    Array.cpp
    Iterator.cpp
    XML.cpp
    Hasher.cpp
    YMD5.cpp
    YSHA1.cpp
    YSHA256.cpp
    Base64.cpp
    Cipher.cpp
    Compressor.cpp
    Math.cpp
    MatchingItem.cpp
    Vector.cpp
    Configuration.cpp
    Message.cpp
    Engine.cpp
    Plugin.cpp
    DataFormat.cpp
    Channel.cpp
    Client.cpp
    ClientLogic.cpp
)
add_library(Yate::yate ALIAS libyate)
set_target_properties(libyate PROPERTIES OUTPUT_NAME yate EXPORT_NAME yate
    VERSION "${PROJECT_VERSION}" SOVERSION "${PROJECT_VERSION}")
target_include_directories(libyate PUBLIC
    "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>"
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/yate>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tables")
target_link_libraries(libyate PUBLIC Threads::Threads
    PRIVATE "$<BUILD_INTERFACE:yate_build_options>" ${CMAKE_DL_LIBS} ${YATE_RESOLVER_LIBRARIES})
target_compile_definitions(libyate PUBLIC ${YATE_PUBLIC_DEFINITIONS}
    PRIVATE ${YATE_PLATFORM_DEFINITIONS} ${YATE_RESOLVER_DEFINITIONS} ${YATE_SCTP_DEFINITIONS})
if(YATE_RTTI STREQUAL "OFF")
    # Внешние модули должны использовать тот же режим RTTI, что и ядро.
    target_compile_options(libyate INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>")
endif()
if(YATE_INTERNAL_REGEX)
    target_sources(libyate PRIVATE regex/regex.c)
    target_include_directories(libyate PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/regex")
    set_source_files_properties(regex/regex.c PROPERTIES COMPILE_DEFINITIONS STDC_HEADERS COMPILE_OPTIONS -UDEBUG)
endif()
if(APPLE)
    enable_language(OBJCXX)
    target_sources(libyate PRIVATE macosx/MacOSXUtils.mm)
    target_include_directories(libyate PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/macosx")
    target_compile_definitions(libyate PRIVATE HAVE_MACOSX_SUPPORT)
    find_library(YATE_FOUNDATION_FRAMEWORK Foundation)
    if(NOT YATE_FOUNDATION_FRAMEWORK)
        message(FATAL_ERROR "The Foundation framework is required on macOS")
    endif()
    target_link_libraries(libyate PRIVATE "${YATE_FOUNDATION_FRAMEWORK}" objc)
endif()
yate_install_rpath(libyate "${CMAKE_INSTALL_LIBDIR}")
install(TARGETS libyate EXPORT YateTargets LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
