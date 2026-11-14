add_library(ilbc STATIC
    anaFilter.c
    iCBSearch.c
    packing.c
    constants.c
    gainquant.c
    iLBC_decode.c
    StateConstructW.c
    createCB.c
    getCBvec.c
    iLBC_encode.c
    StateSearchW.c
    doCPLC.c
    helpfun.c
    syntFilter.c
    enhancer.c
    hpInput.c
    LPCdecode.c
    filter.c
    hpOutput.c
    LPCencode.c
    FrameClassify.c
    iCBConstruct.c
    lsf.c
)
target_link_libraries(ilbc PRIVATE yate_build_options)
target_include_directories(ilbc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ilbc PRIVATE -fsigned-char)
