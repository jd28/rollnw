 add_library(stb_image STATIC
    stb_impl.c
    image_DXT.c
    wfETC.c
 )

 set_target_properties(stb_image PROPERTIES LINKER_LANGUAGE C)
