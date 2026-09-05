add_executable(nfifo_test nfifo_test.c)
target_link_libraries(nfifo_test PRIVATE Ncore::ncore)
add_test(NAME nfifo_test COMMAND nfifo_test)

add_executable(nthread_message_test nthread_message_test.c)
target_link_libraries(nthread_message_test PRIVATE Ncore::ncore)
add_test(NAME nthread_message_test COMMAND nthread_message_test)

add_executable(nlog_test nlog_test.c)
target_link_libraries(nlog_test PRIVATE Ncore::ncore)
add_test(NAME nlog_test COMMAND nlog_test)
