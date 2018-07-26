macro( ADD_FILTER group_name )
    source_group( ${group_name} FILES ${ARGN} )
    set( SRC_FILES ${SRC_FILES} ${ARGN} )
endmacro()