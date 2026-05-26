add_executable(coap-pp-tests
    test_transport_interface.cpp
    test_pdu.cpp
    test_serialize.cpp
    test_messenger.cpp
    test_server.cpp
)

target_link_libraries(coap-pp-tests PRIVATE
    coap-pp
    GTest::gtest_main
)

target_compile_features(coap-pp-tests PRIVATE cxx_std_20)

include(GoogleTest)
gtest_discover_tests(coap-pp-tests)
