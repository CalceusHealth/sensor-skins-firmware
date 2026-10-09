include("${CMAKE_CURRENT_LIST_DIR}/rule.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/file.cmake")

set(Charger_Debug_library_list )

# Handle files with suffix s, for group Debug-avr-gcc
if(Charger_Debug_Debug_avr_gcc_FILE_TYPE_assemble)
add_library(Charger_Debug_Debug_avr_gcc_assemble OBJECT ${Charger_Debug_Debug_avr_gcc_FILE_TYPE_assemble})
    Charger_Debug_Debug_avr_gcc_assemble_rule(Charger_Debug_Debug_avr_gcc_assemble)
    list(APPEND Charger_Debug_library_list "$<TARGET_OBJECTS:Charger_Debug_Debug_avr_gcc_assemble>")

endif()

# Handle files with suffix S, for group Debug-avr-gcc
if(Charger_Debug_Debug_avr_gcc_FILE_TYPE_assembleWithPreprocess)
add_library(Charger_Debug_Debug_avr_gcc_assembleWithPreprocess OBJECT ${Charger_Debug_Debug_avr_gcc_FILE_TYPE_assembleWithPreprocess})
    Charger_Debug_Debug_avr_gcc_assembleWithPreprocess_rule(Charger_Debug_Debug_avr_gcc_assembleWithPreprocess)
    list(APPEND Charger_Debug_library_list "$<TARGET_OBJECTS:Charger_Debug_Debug_avr_gcc_assembleWithPreprocess>")

endif()

# Handle files with suffix [cC], for group Debug-avr-gcc
if(Charger_Debug_Debug_avr_gcc_FILE_TYPE_compile)
add_library(Charger_Debug_Debug_avr_gcc_compile OBJECT ${Charger_Debug_Debug_avr_gcc_FILE_TYPE_compile})
    Charger_Debug_Debug_avr_gcc_compile_rule(Charger_Debug_Debug_avr_gcc_compile)
    list(APPEND Charger_Debug_library_list "$<TARGET_OBJECTS:Charger_Debug_Debug_avr_gcc_compile>")

endif()

# Handle files with suffix cpp, for group Debug-avr-gcc
if(Charger_Debug_Debug_avr_gcc_FILE_TYPE_compile_cpp)
add_library(Charger_Debug_Debug_avr_gcc_compile_cpp OBJECT ${Charger_Debug_Debug_avr_gcc_FILE_TYPE_compile_cpp})
    Charger_Debug_Debug_avr_gcc_compile_cpp_rule(Charger_Debug_Debug_avr_gcc_compile_cpp)
    list(APPEND Charger_Debug_library_list "$<TARGET_OBJECTS:Charger_Debug_Debug_avr_gcc_compile_cpp>")

endif()

# Handle files with suffix elf, for group Debug-avr-gcc
if(Charger_Debug_Debug_avr_gcc_FILE_TYPE_objcopy_ihex)
add_library(Charger_Debug_Debug_avr_gcc_objcopy_ihex OBJECT ${Charger_Debug_Debug_avr_gcc_FILE_TYPE_objcopy_ihex})
    Charger_Debug_Debug_avr_gcc_objcopy_ihex_rule(Charger_Debug_Debug_avr_gcc_objcopy_ihex)
    list(APPEND Charger_Debug_library_list "$<TARGET_OBJECTS:Charger_Debug_Debug_avr_gcc_objcopy_ihex>")

endif()

# Handle files with suffix elf, for group Debug-avr-gcc
if(Charger_Debug_Debug_avr_gcc_FILE_TYPE_objcopy_eep)
add_library(Charger_Debug_Debug_avr_gcc_objcopy_eep OBJECT ${Charger_Debug_Debug_avr_gcc_FILE_TYPE_objcopy_eep})
    Charger_Debug_Debug_avr_gcc_objcopy_eep_rule(Charger_Debug_Debug_avr_gcc_objcopy_eep)
    list(APPEND Charger_Debug_library_list "$<TARGET_OBJECTS:Charger_Debug_Debug_avr_gcc_objcopy_eep>")

endif()

# Handle files with suffix elf, for group Debug-avr-gcc
if(Charger_Debug_Debug_avr_gcc_FILE_TYPE_objcopy_lss)
add_library(Charger_Debug_Debug_avr_gcc_objcopy_lss OBJECT ${Charger_Debug_Debug_avr_gcc_FILE_TYPE_objcopy_lss})
    Charger_Debug_Debug_avr_gcc_objcopy_lss_rule(Charger_Debug_Debug_avr_gcc_objcopy_lss)
    list(APPEND Charger_Debug_library_list "$<TARGET_OBJECTS:Charger_Debug_Debug_avr_gcc_objcopy_lss>")

endif()

# Handle files with suffix elf, for group Debug-avr-gcc
if(Charger_Debug_Debug_avr_gcc_FILE_TYPE_objcopy_srec)
add_library(Charger_Debug_Debug_avr_gcc_objcopy_srec OBJECT ${Charger_Debug_Debug_avr_gcc_FILE_TYPE_objcopy_srec})
    Charger_Debug_Debug_avr_gcc_objcopy_srec_rule(Charger_Debug_Debug_avr_gcc_objcopy_srec)
    list(APPEND Charger_Debug_library_list "$<TARGET_OBJECTS:Charger_Debug_Debug_avr_gcc_objcopy_srec>")

endif()

# Handle files with suffix elf, for group Debug-avr-gcc
if(Charger_Debug_Debug_avr_gcc_FILE_TYPE_objcopy_sig)
add_library(Charger_Debug_Debug_avr_gcc_objcopy_sig OBJECT ${Charger_Debug_Debug_avr_gcc_FILE_TYPE_objcopy_sig})
    Charger_Debug_Debug_avr_gcc_objcopy_sig_rule(Charger_Debug_Debug_avr_gcc_objcopy_sig)
    list(APPEND Charger_Debug_library_list "$<TARGET_OBJECTS:Charger_Debug_Debug_avr_gcc_objcopy_sig>")

endif()


# Main target for this project
add_executable(Charger_Debug_image_mliI62_Z ${Charger_Debug_library_list})

set_target_properties(Charger_Debug_image_mliI62_Z PROPERTIES
    OUTPUT_NAME "Debug"
    SUFFIX ".elf"
    ADDITIONAL_CLEAN_FILES "${output_extensions}"
    RUNTIME_OUTPUT_DIRECTORY "${Charger_Debug_output_dir}")
target_link_libraries(Charger_Debug_image_mliI62_Z PRIVATE ${Charger_Debug_Debug_avr_gcc_FILE_TYPE_link})

#Add objcopy steps
Charger_Debug_objcopy_ihex_rule(Charger_Debug_image_mliI62_Z)
Charger_Debug_objcopy_eep_rule(Charger_Debug_image_mliI62_Z)
Charger_Debug_objcopy_lss_rule(Charger_Debug_image_mliI62_Z)
Charger_Debug_objcopy_srec_rule(Charger_Debug_image_mliI62_Z)
Charger_Debug_objcopy_sig_rule(Charger_Debug_image_mliI62_Z)
# Add the link options from the rule file.
Charger_Debug_link_rule( Charger_Debug_image_mliI62_Z)


