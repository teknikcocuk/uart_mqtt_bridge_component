# components/uart_comm/CMakeLists.txt
idf_component_register(SRCS "uart_comm.c"
                    INCLUDE_DIRS "include"
                    REQUIRES driver freertos log)