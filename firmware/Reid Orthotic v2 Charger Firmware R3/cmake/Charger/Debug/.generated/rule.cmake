# The following functions contains all the flags passed to the different build stages.

set(PACK_REPO_PATH "/home/grey/.mchp_packs" CACHE PATH "Path to the root of a pack repository.")

function(Charger_Debug_Debug_avr_gcc_assemble_rule target)
    set(options
        "-x"
        "assembler-with-cpp"
        "${MP_EXTRA_AS_PRE}"
        "-mmcu=attiny1616"
        "-B${PACK_REPO_PATH}/Microchip/ATtiny_DFP/3.4.278/gcc/dev/attiny1616"
        "-c"
        "-Wa,--defsym=__MPLAB_BUILD=1${MP_EXTRA_AS_POST}")
    list(REMOVE_ITEM options "")
    target_compile_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target}
        PRIVATE "__ATtiny1616__"
        PRIVATE "Debug=Debug")
    target_include_directories(${target} PRIVATE "${PACK_REPO_PATH}/Microchip/ATtiny_DFP/3.4.278/include")
endfunction()
function(Charger_Debug_Debug_avr_gcc_assembleWithPreprocess_rule target)
    set(options
        "-x"
        "assembler-with-cpp"
        "${MP_EXTRA_AS_PRE}"
        "-mmcu=attiny1616"
        "-B${PACK_REPO_PATH}/Microchip/ATtiny_DFP/3.4.278/gcc/dev/attiny1616"
        "-c"
        "-Wa,--defsym=__MPLAB_BUILD=1${MP_EXTRA_AS_POST}")
    list(REMOVE_ITEM options "")
    target_compile_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target}
        PRIVATE "__ATtiny1616__"
        PRIVATE "Debug=Debug")
    target_include_directories(${target} PRIVATE "${PACK_REPO_PATH}/Microchip/ATtiny_DFP/3.4.278/include")
endfunction()
function(Charger_Debug_Debug_avr_gcc_compile_rule target)
    set(options
        "-g"
        "-gdwarf-2"
        "${MP_EXTRA_CC_PRE}"
        "-mmcu=attiny1616"
        "-B${PACK_REPO_PATH}/Microchip/ATtiny_DFP/3.4.278/gcc/dev/attiny1616"
        "-x"
        "c"
        "-c"
        "-funsigned-char"
        "-funsigned-bitfields"
        "-O1"
        "-ffunction-sections"
        "-fdata-sections"
        "-fpack-struct"
        "-fshort-enums"
        "-Wall")
    list(REMOVE_ITEM options "")
    target_compile_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target}
        PRIVATE "__ATtiny1616__"
        PRIVATE "Debug=Debug")
    target_include_directories(${target} PRIVATE "${PACK_REPO_PATH}/Microchip/ATtiny_DFP/3.4.278/include")
endfunction()
function(Charger_Debug_Debug_avr_gcc_compile_cpp_rule target)
    set(options
        "-g"
        "-gdwarf-2"
        "${MP_EXTRA_CC_PRE}"
        "-mmcu=attiny1616"
        "-B${PACK_REPO_PATH}/Microchip/ATtiny_DFP/3.4.278/gcc/dev/attiny1616"
        "-x"
        "c++"
        "-c"
        "-O0"
        "-ffunction-sections"
        "-fdata-sections")
    list(REMOVE_ITEM options "")
    target_compile_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target}
        PRIVATE "__ATtiny1616__"
        PRIVATE "Debug=Debug")
    target_include_directories(${target} PRIVATE "${PACK_REPO_PATH}/Microchip/ATtiny_DFP/3.4.278/include")
endfunction()
function(Charger_Debug_link_rule target)
    set(options
        "-gdwarf-2"
        "${MP_EXTRA_LD_PRE}"
        "-mmcu=attiny1616"
        "-B${PACK_REPO_PATH}/Microchip/ATtiny_DFP/3.4.278/gcc/dev/attiny1616"
        "-Wl,-Map=mem.map"
        "-Wl,--defsym=__MPLAB_BUILD=1${MP_EXTRA_LD_POST}"
        "-Wl,--gc-sections")
    list(REMOVE_ITEM options "")
    target_link_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target}
        PRIVATE "__ATtiny1616__"
        PRIVATE "Debug=Debug")
    target_link_libraries(${target} PRIVATE "m")
endfunction()
function(Charger_Debug_objcopy_ihex_rule target)
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND ${OBJCOPY}
        ARGS --output-target=ihex ${Charger_Debug_image_name} ${Charger_Debug_image_base_name}.hex
        WORKING_DIRECTORY ${Charger_Debug_output_dir})
endfunction()
function(Charger_Debug_objcopy_eep_rule target)
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND ${OBJCOPY}
        ARGS --only-section=.eeprom --change-section-lma .eeprom=0 --no-change-warnings --output-target=ihex ${Charger_Debug_image_name} ${Charger_Debug_image_base_name}.eep
        WORKING_DIRECTORY ${Charger_Debug_output_dir})
endfunction()
function(Charger_Debug_objcopy_lss_rule target)
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND ${OBJDUMP}
        ARGS --disassemble --wide --demangle --line-numbers --section-headers --source ${Charger_Debug_image_name} > ${Charger_Debug_image_base_name}.lss
        WORKING_DIRECTORY ${Charger_Debug_output_dir})
endfunction()
function(Charger_Debug_objcopy_srec_rule target)
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND ${OBJCOPY}
        ARGS --output-target=srec --remove-section=.eeprom --remove-section=.fuse --remove-section=.lock --remove-section=.signature ${Charger_Debug_image_name} ${Charger_Debug_image_base_name}.srec
        WORKING_DIRECTORY ${Charger_Debug_output_dir})
endfunction()
function(Charger_Debug_objcopy_sig_rule target)
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND ${OBJCOPY}
        ARGS --only-section=.user_signatures --change-section-lma .user_signatures=0 --no-change-warnings --output-target=ihex ${Charger_Debug_image_name} ${Charger_Debug_image_base_name}.usersignatures
        WORKING_DIRECTORY ${Charger_Debug_output_dir})
endfunction()
