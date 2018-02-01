file(GLOB openflow_sources *.cpp)

add_tasvir_exec(tasvir_openflow ${openflow_sources})
target_compile_definitions(tasvir_openflow PRIVATE TASVIR)

#add_tasvir_exec(tasvir_openflow_skew ${openflow_sources})
#target_compile_definitions(tasvir_openflow_skew PRIVATE TASVIR EVAL_SKEW=1)

add_tasvir_exec(openflow ${openflow_sources})

#add_tasvir_exec(openflow_skew ${openflow_sources})
#target_compile_definitions(openflow_skew PRIVATE EVAL_SKEW=1)

#add_tasvir_exec(openflow_big ${openflow_sources})
#target_compile_definitions(openflow_big PRIVATE BIG_LOCK=1)

#add_tasvir_exec(openflow_big_skew ${openflow_sources})
#target_compile_definitions(openflow_big_skew PRIVATE BIG_LOCK=1 EVAL_SKEW=1)
