include_directories(../external/catch/single_include .)

set(HEADERS
    util/event_loop.hpp
    util/index_helpers.hpp
    util/test_file.hpp
)

set(SOURCES
    collection_change_indices.cpp
    handover.cpp
    index_set.cpp
    list.cpp
    main.cpp
    migrations.cpp
    object_store.cpp
    parser.cpp
    realm.cpp
    results.cpp
    schema.cpp
    transaction_log_parsing.cpp
    util/event_loop.cpp
    util/test_file.cpp
)

add_executable(tests ${SOURCES} ${HEADERS})
target_compile_definitions(tests PRIVATE ${PLATFORM_DEFINES})
target_link_libraries(tests realm-object-store ${PLATFORM_LIBRARIES})

create_coverage_target(generate-coverage tests)

add_custom_target(run-tests USES_TERMINAL DEPENDS tests COMMAND ./tests)

add_subdirectory(notifications-fuzzer)
