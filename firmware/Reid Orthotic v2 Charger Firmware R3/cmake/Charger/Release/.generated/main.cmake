include("${CMAKE_CURRENT_LIST_DIR}/rule.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/file.cmake")

set(Charger_Release_library_list )

# Handle files with suffix s, for group Release-avr-gcc
if(Charger_Release_Release_avr_gcc_FILE_TYPE_assemble)
add_library(Charger_Release_Release_avr_gcc_assemble OBJECT ${Charger_Release_Release_avr_gcc_FILE_TYPE_assemble})
    Charger_Release_Release_avr_gcc_assemble_rule(Charger_Release_Release_avr_gcc_assemble)
    list(APPEND Charger_Release_library_list "$<TARGET_OBJECTS:Charger_Release_Release_avr_gcc_assemble>")

endif()

# Handle files with suffix S, for group Release-avr-gcc
if(Charger_Release_Release_avr_gcc_FILE_TYPE_assembleWithPreprocess)
add_library(Charger_Release_Release_avr_gcc_assembleWithPreprocess OBJECT ${Charger_Release_Release_avr_gcc_FILE_TYPE_assembleWithPreprocess})
    Charger_Release_Release_avr_gcc_assembleWithPreprocess_rule(Charger_Release_Release_avr_gcc_assembleWithPreprocess)
    list(APPEND Charger_Release_library_list "$<TARGET_OBJECTS:Charger_Release_Release_avr_gcc_assembleWithPreprocess>")

endif()

# Handle files with suffix [cC], for group Release-avr-gcc
if(Charger_Release_Release_avr_gcc_FILE_TYPE_compile)
add_library(Charger_Release_Release_avr_gcc_compile OBJECT ${Charger_Release_Release_avr_gcc_FILE_TYPE_compile})
    Charger_Release_Release_avr_gcc_compile_rule(Charger_Release_Release_avr_gcc_compile)
    list(APPEND Charger_Release_library_list "$<TARGET_OBJECTS:Charger_Release_Release_avr_gcc_compile>")

endif()

# Handle files with suffix cpp, for group Release-avr-gcc
if(Charger_Release_Release_avr_gcc_FILE_TYPE_compile_cpp)
add_library(Charger_Release_Release_avr_gcc_compile_cpp OBJECT ${Charger_Release_Release_avr_gcc_FILE_TYPE_compile_cpp})
    Charger_Release_Release_avr_gcc_compile_cpp_rule(Charger_Release_Release_avr_gcc_compile_cpp)
    list(APPEND Charger_Release_library_list "$<TARGET_OBJECTS:Charger_Release_Release_avr_gcc_compile_cpp>")

endif()

# Handle files with suffix elf, for group Release-avr-gcc
if(Charger_Release_Release_avr_gcc_FILE_TYPE_objcopy_ihex)
add_library(Charger_Release_Release_avr_gcc_objcopy_ihex OBJECT ${Charger_Release_Release_avr_gcc_FILE_TYPE_objcopy_ihex})
    Charger_Release_Release_avr_gcc_objcopy_ihex_rule(Charger_Release_Release_avr_gcc_objcopy_ihex)
    list(APPEND Charger_Release_library_list "$<TARGET_OBJECTS:Charger_Release_Release_avr_gcc_objcopy_ihex>")

endif()

# Handle files with suffix elf, for group Release-avr-gcc
if(Charger_Release_Release_avr_gcc_FILE_TYPE_objcopy_eep)
add_library(Charger_Release_Release_avr_gcc_objcopy_eep OBJECT ${Charger_Release_Release_avr_gcc_FILE_TYPE_objcopy_eep})
    Charger_Release_Release_avr_gcc_objcopy_eep_rule(Charger_Release_Release_avr_gcc_objcopy_eep)
    list(APPEND Charger_Release_library_list "$<TARGET_OBJECTS:Charger_Release_Release_avr_gcc_objcopy_eep>")

endif()

# Handle files with suffix elf, for group Release-avr-gcc
if(Charger_Release_Release_avr_gcc_FILE_TYPE_objcopy_lss)
add_library(Charger_Release_Release_avr_gcc_objcopy_lss OBJECT ${Charger_Release_Release_avr_gcc_FILE_TYPE_objcopy_lss})
    Charger_Release_Release_avr_gcc_objcopy_lss_rule(Charger_Release_Release_avr_gcc_objcopy_lss)
    list(APPEND Charger_Release_library_list "$<TARGET_OBJECTS:Charger_Release_Release_avr_gcc_objcopy_lss>")

endif()

# Handle files with suffix elf, for group Release-avr-gcc
if(Charger_Release_Release_avr_gcc_FILE_TYPE_objcopy_srec)
add_library(Charger_Release_Release_avr_gcc_objcopy_srec OBJECT ${Charger_Release_Release_avr_gcc_FILE_TYPE_objcopy_srec})
    Charger_Release_Release_avr_gcc_objcopy_srec_rule(Charger_Release_Release_avr_gcc_objcopy_srec)
    list(APPEND Charger_Release_library_list "$<TARGET_OBJECTS:Charger_Release_Release_avr_gcc_objcopy_srec>")

endif()

# Handle files with suffix elf, for group Release-avr-gcc
if(Charger_Release_Release_avr_gcc_FILE_TYPE_objcopy_sig)
add_library(Charger_Release_Release_avr_gcc_objcopy_sig OBJECT ${Charger_Release_Release_avr_gcc_FILE_TYPE_objcopy_sig})
    Charger_Release_Release_avr_gcc_objcopy_sig_rule(Charger_Release_Release_avr_gcc_objcopy_sig)
    list(APPEND Charger_Release_library_list "$<TARGET_OBJECTS:Charger_Release_Release_avr_gcc_objcopy_sig>")

endif()


# Main target for this project
add_executable(Charger_Release_image_Egi0NzEI ${Charger_Release_library_list})

set_target_properties(Charger_Release_image_Egi0NzEI PROPERTIES
    OUTPUT_NAME "Release"
    SUFFIX ".elf"
    ADDITIONAL_CLEAN_FILES "${output_extensions}"
    RUNTIME_OUTPUT_DIRECTORY "${Charger_Release_output_dir}")
target_link_libraries(Charger_Release_image_Egi0NzEI PRIVATE ${Charger_Release_Release_avr_gcc_FILE_TYPE_link})

#Add objcopy steps
Charger_Release_objcopy_ihex_rule(Charger_Release_image_Egi0NzEI)
Charger_Release_objcopy_eep_rule(Charger_Release_image_Egi0NzEI)
Charger_Release_objcopy_lss_rule(Charger_Release_image_Egi0NzEI)
Charger_Release_objcopy_srec_rule(Charger_Release_image_Egi0NzEI)
Charger_Release_objcopy_sig_rule(Charger_Release_image_Egi0NzEI)
# Add the link options from the rule file.
Charger_Release_link_rule( Charger_Release_image_Egi0NzEI)


