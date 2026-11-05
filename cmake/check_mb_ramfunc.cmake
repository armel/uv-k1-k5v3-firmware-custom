# Validate the multiboot restore stub after linking. The application flash is
# unavailable while this code runs, so every direct control-flow target and
# every symbolic code/data reference must remain inside .mb_ramfunc.

foreach(required OBJDUMP ELF MAP BINARY_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "check_mb_ramfunc: missing -D${required}=...")
    endif()
endforeach()

if(NOT EXISTS "${ELF}")
    message(FATAL_ERROR "check_mb_ramfunc: ELF not found: ${ELF}")
endif()
if(NOT EXISTS "${MAP}")
    message(FATAL_ERROR "check_mb_ramfunc: map file not found: ${MAP}")
endif()

execute_process(
    COMMAND "${OBJDUMP}" -h "${ELF}"
    RESULT_VARIABLE headers_result
    OUTPUT_VARIABLE headers
    ERROR_VARIABLE headers_error
)
if(NOT headers_result EQUAL 0)
    message(FATAL_ERROR
        "check_mb_ramfunc: objdump section scan failed (${headers_result}):\n${headers_error}")
endif()

string(REGEX MATCH
    "[ \t]\\.mb_ramfunc[ \t]+([0-9A-Fa-f]+)[ \t]+([0-9A-Fa-f]+)"
    section_header "${headers}")
if(section_header STREQUAL "")
    message(FATAL_ERROR "check_mb_ramfunc: .mb_ramfunc not found in ${ELF}")
endif()
set(section_size_hex "${CMAKE_MATCH_1}")
set(section_start_hex "${CMAKE_MATCH_2}")
math(EXPR section_size "0x${section_size_hex}")
math(EXPR section_start "0x${section_start_hex}")
math(EXPR section_end "${section_start} + ${section_size}")
math(EXPR section_end_hex "${section_end}" OUTPUT_FORMAT HEXADECIMAL)
if(section_size EQUAL 0)
    message(FATAL_ERROR "check_mb_ramfunc: .mb_ramfunc is empty in an overlay build")
endif()

execute_process(
    COMMAND "${OBJDUMP}" --no-show-raw-insn -d -j .mb_ramfunc "${ELF}"
    RESULT_VARIABLE disassembly_result
    OUTPUT_VARIABLE disassembly
    ERROR_VARIABLE disassembly_error
)
if(NOT disassembly_result EQUAL 0)
    message(FATAL_ERROR
        "check_mb_ramfunc: disassembly failed (${disassembly_result}):\n${disassembly_error}")
endif()

# Check all Thumb branch forms. An indirect BLX/BX cannot be proven safe; only
# BX LR (a normal function return) is accepted. Direct branches must stay in the
# linked RAM section, including compiler-generated tail calls and veneers.
string(REPLACE "\n" ";" disassembly_lines "${disassembly}")
set(branch_count 0)
foreach(line IN LISTS disassembly_lines)
    if(line MATCHES
       "^[ \t]*[0-9A-Fa-f]+:[ \t]+([A-Za-z0-9.]+)([ \t]+(.*))?")
        set(mnemonic "${CMAKE_MATCH_1}")
        set(operands "${CMAKE_MATCH_3}")

        if(mnemonic MATCHES
           "^(b|bl|blx|bx|beq|bne|bcs|bcc|bhs|blo|bmi|bpl|bvs|bvc|bhi|bls|bge|blt|bgt|ble)(\\.[nw])?$"
           OR mnemonic MATCHES "^(cbz|cbnz)$")
            math(EXPR branch_count "${branch_count} + 1")

            if(mnemonic MATCHES "^bx(\\.[nw])?$")
                string(STRIP "${operands}" register_name)
                if(NOT register_name STREQUAL "lr" AND NOT register_name STREQUAL "r14")
                    message(FATAL_ERROR
                        "check_mb_ramfunc: unsafe indirect branch in RAM stub:\n${line}")
                endif()
                continue()
            endif()

            # BL/B operands contain the address directly; CBZ/CBNZ put it after
            # the register and comma. In both forms it is the final numeric
            # operand before objdump's optional <symbol> annotation.
            if(NOT operands MATCHES
               "(^|[, \t])((0[xX])?[0-9A-Fa-f]+)([ \t]+<[^>]+>)?[ \t]*$")
                message(FATAL_ERROR
                    "check_mb_ramfunc: unresolved/indirect branch in RAM stub:\n${line}")
            endif()
            set(target_hex "${CMAKE_MATCH_2}")
            string(REGEX REPLACE "^0[xX]" "" target_hex "${target_hex}")
            math(EXPR target "0x${target_hex}")
            if(target LESS section_start OR NOT target LESS section_end)
                message(FATAL_ERROR
                    "check_mb_ramfunc: branch leaves .mb_ramfunc:\n${line}\n"
                    "Allowed range: 0x${section_start_hex}..${section_end_hex}")
            endif()
        endif()
    endif()
endforeach()

# Locate the exact input object from the linker map rather than assuming a
# generator-specific CMakeFiles path. Any relocation in .MBRamFunc would be an
# external symbolic reference (code or data), because all approved helpers share
# this same input section and their local branches are assembler-resolved.
file(READ "${MAP}" map_contents)
string(REGEX MATCH
    "[^\r\n]*\\.MBRamFunc[^\r\n]*mb_flash\\.c\\.(obj|o)"
    object_line "${map_contents}")
if(object_line STREQUAL "")
    message(FATAL_ERROR
        "check_mb_ramfunc: mb_flash object not found in ${MAP} "
        "(the overlay safety gate requires a non-LTO input object)")
endif()
string(REGEX MATCH "[^ \t]+mb_flash\\.c\\.(obj|o)" object_path "${object_line}")
if(NOT IS_ABSOLUTE "${object_path}")
    set(object_path "${BINARY_DIR}/${object_path}")
endif()
if(NOT EXISTS "${object_path}")
    message(FATAL_ERROR "check_mb_ramfunc: input object not found: ${object_path}")
endif()

execute_process(
    COMMAND "${OBJDUMP}" -r -j .MBRamFunc "${object_path}"
    RESULT_VARIABLE relocations_result
    OUTPUT_VARIABLE relocations
    ERROR_VARIABLE relocations_error
)
if(NOT relocations_result EQUAL 0)
    message(FATAL_ERROR
        "check_mb_ramfunc: relocation scan failed (${relocations_result}):\n${relocations_error}")
endif()
if(relocations MATCHES "R_ARM_")
    message(FATAL_ERROR
        "check_mb_ramfunc: .MBRamFunc has an external code/data reference:\n${relocations}")
endif()

message(STATUS
    "Multiboot RAM stub isolation OK: ${section_size} bytes, ${branch_count} checked branches")
