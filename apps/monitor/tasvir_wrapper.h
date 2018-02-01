#include "tasvir.h"

// Enable/Disable log writting or synching for experiments purpose

#if !(NO_LOG)
#define tasvir_log_write_wrapper(data, size) \
	do { tasvir_log_write(data, size); } while (0)
#else 
#define tasvir_log_write_wrapper ((void)0);
#endif

#if !(NO_SERVICE)
#define tasvir_service_wrapper() \
	do { tasvir_service(); } while (0)
#else
#define tasvir_service_wrapper() ((void)0);
#endif

