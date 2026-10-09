# The following variables contains the files used by the different stages of the build process.
set(Charger_Debug_Debug_avr_gcc_FILE_TYPE_assemble)
set_source_files_properties(${Charger_Debug_Debug_avr_gcc_FILE_TYPE_assemble} PROPERTIES LANGUAGE ASM)

# For assembly files, add "." to the include path for each file so that .include with a relative path works
foreach(source_file ${Charger_Debug_Debug_avr_gcc_FILE_TYPE_assemble})
        set_source_files_properties(${source_file} PROPERTIES INCLUDE_DIRECTORIES "$<PATH:NORMAL_PATH,$<PATH:REMOVE_FILENAME,${source_file}>>")
endforeach()

set(Charger_Debug_Debug_avr_gcc_FILE_TYPE_assembleWithPreprocess)
set_source_files_properties(${Charger_Debug_Debug_avr_gcc_FILE_TYPE_assembleWithPreprocess} PROPERTIES LANGUAGE ASM)

# For assembly files, add "." to the include path for each file so that .include with a relative path works
foreach(source_file ${Charger_Debug_Debug_avr_gcc_FILE_TYPE_assembleWithPreprocess})
        set_source_files_properties(${source_file} PROPERTIES INCLUDE_DIRECTORIES "$<PATH:NORMAL_PATH,$<PATH:REMOVE_FILENAME,${source_file}>>")
endforeach()

set(Charger_Debug_Debug_avr_gcc_FILE_TYPE_compile
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../coil.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../gpio.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../main.c")
set_source_files_properties(${Charger_Debug_Debug_avr_gcc_FILE_TYPE_compile} PROPERTIES LANGUAGE C)
set(Charger_Debug_Debug_avr_gcc_FILE_TYPE_compile_cpp)
set_source_files_properties(${Charger_Debug_Debug_avr_gcc_FILE_TYPE_compile_cpp} PROPERTIES LANGUAGE CXX)
set(Charger_Debug_Debug_avr_gcc_FILE_TYPE_link)
set(Charger_Debug_Debug_avr_gcc_FILE_TYPE_objcopy_ihex)
set(Charger_Debug_Debug_avr_gcc_FILE_TYPE_objcopy_eep)
set(Charger_Debug_Debug_avr_gcc_FILE_TYPE_objcopy_lss)
set(Charger_Debug_Debug_avr_gcc_FILE_TYPE_objcopy_srec)
set(Charger_Debug_Debug_avr_gcc_FILE_TYPE_objcopy_sig)
set(Charger_Debug_image_name "Debug.elf")
set(Charger_Debug_image_base_name "Debug")

# The output directory of the final image.
set(Charger_Debug_output_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../../out/Charger")

# The full path to the final image.
set(Charger_Debug_full_path_to_image ${Charger_Debug_output_dir}/${Charger_Debug_image_name})

# Potential output file extensions
set(output_extensions
    .hex
    .lss
    .eep
    .srec
    .usersignatures)
list(TRANSFORM output_extensions PREPEND "${Charger_Debug_output_dir}/${Charger_Debug_image_base_name}")
