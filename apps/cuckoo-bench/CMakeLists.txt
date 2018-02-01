file(GLOB cuckoo_sources . *.cc *.c)
add_tasvir_exec(cuckoo-bench ${cuckoo_sources})
set_property(TARGET cuckoo-bench PROPERTY CXX_STANDARD 14)
target_link_libraries(cuckoo-bench pthread rt)
