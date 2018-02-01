file(GLOB monitor_sources *.cpp)

add_tasvir_exec(tasvir_monitor ${monitor_sources})
target_compile_definitions(tasvir_monitor PRIVATE NO_LOG=0 NO_SERVICE=0)

#add_tasvir_exec(tasvir_monitor-nolog ${monitor_sources})
#target_compile_definitions(tasvir_monitor-nolog PRIVATE NO_LOG=1 NO_SERVICE=0)

#add_tasvir_exec(tasvir_monitor-nosync ${monitor_sources})
#target_compile_definitions(tasvir_monitor-nosync PRIVATE NO_LOG=0 NO_SERVICE=1)

#add_tasvir_exec(tasvir_monitor-nothing ${monitor_sources})
#target_compile_definitions(tasvir_monitor-nothing PRIVATE NO_LOG=1 NO_SERVICE=1)
