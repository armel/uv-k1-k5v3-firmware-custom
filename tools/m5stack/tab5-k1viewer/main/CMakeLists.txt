idf_component_register(SRCS "main.c" "gps.c" "pl2303_vcp.c"
                       INCLUDE_DIRS ".")

# PNGs from assets/ converted to raw RGB565 at build time and linked into
# .rodata as <symbol>_start / <symbol>_end (see scripts/png_to_rgb565.py).
idf_build_get_property(python PYTHON)
set(image_assets "${CMAKE_CURRENT_LIST_DIR}/../assets")
set(image_scripts "${CMAKE_CURRENT_LIST_DIR}/../scripts")

function(embed_rgb565 name symbol width height)
    set(png "${image_assets}/${name}.png")
    set(bin "${CMAKE_CURRENT_BINARY_DIR}/${symbol}.bin")
    set(asm "${CMAKE_CURRENT_BINARY_DIR}/${symbol}.S")

    add_custom_command(OUTPUT "${bin}" "${asm}"
        COMMAND "${python}" -B "${image_scripts}/png_to_rgb565.py" "${png}" "${bin}"
                --size ${width} ${height} --asm "${asm}" --symbol ${symbol}
        DEPENDS "${png}"
                "${image_scripts}/png_to_rgb565.py"
                "${image_scripts}/gen_uv_k1_skin.py"
        COMMENT "Converting ${name}.png to RGB565"
        VERBATIM)
    target_sources(${COMPONENT_LIB} PRIVATE "${asm}")
endfunction()

# Image list and sizes, written by scripts/gen_uv_k1_skin.py.
include("${CMAKE_CURRENT_LIST_DIR}/uv_k1_skin_images.cmake")
