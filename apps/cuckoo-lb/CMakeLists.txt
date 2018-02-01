file(GLOB cuckoo_sources . *.cc *.c)
add_tasvir_exec(cuckoo-lb ${cuckoo_sources})
set_property(TARGET cuckoo-lb PROPERTY CXX_STANDARD 14)
target_link_libraries(cuckoo-lb pthread rt)
