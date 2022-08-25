####################
# Nodeset Examples #
####################

###################
# Custom XML      #
###################

set(FILE_CSV_DIRPREFIX ${PROJECT_SOURCE_DIR}/nodeset)
set(FILE_BSD_DIRPREFIX ${PROJECT_SOURCE_DIR}/nodeset)
set(FILE_NS_DIRPREFIX ${PROJECT_SOURCE_DIR}/nodeset)

if(NOT CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    # needed or cmake doesn't recognize dependencies of generated files
    set(PROJECT_BINARY_DIR ${CMAKE_BINARY_DIR})
endif()




###################
# PLCopen Nodeset #
###################

if(NOT CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(FILE_CSV_DIRPREFIX ${open62541_NODESET_DIR})
    set(FILE_BSD_PLCOPEN_DIRPREFIX ${open62541_NODESET_DIR})
    set(FILE_BSD_POWERLINK_DIRPREFIX ${PROJECT_SOURCE_DIR}/nodeset)
    set(FILE_NS_DIRPREFIX ${open62541_NODESET_DIR})
else()
    set(FILE_CSV_DIRPREFIX ${UA_NODESET_DIR})
    set(FILE_BSD_PLCOPEN_DIRPREFIX ${UA_NODESET_DIR})
    set(FILE_BSD_POWERLINK_DIRPREFIX ${PROJECT_SOURCE_DIR}/nodeset)
    set(FILE_NS_DIRPREFIX ${UA_NODESET_DIR})
endif()

# PLCopen requires the full ns0 as basis
if(UA_NAMESPACE_ZERO STREQUAL "FULL")

    # Generate types and namespace for DI
    ua_generate_nodeset_and_datatypes(
        NAME "di"
        FILE_CSV "${FILE_CSV_DIRPREFIX}/DI/OpcUaDiModel.csv"
        FILE_BSD "${FILE_BSD_PLCOPEN_DIRPREFIX}/DI/Opc.Ua.Di.Types.bsd"
        NAMESPACE_MAP "2:http://opcfoundation.org/UA/DI/"
        FILE_NS "${FILE_NS_DIRPREFIX}/DI/Opc.Ua.Di.NodeSet2.xml"
        INTERNAL
    )

    #######################################################


    # generate euromap83 namespace which is using DI
    ua_generate_nodeset_and_datatypes(
            NAME "euromap83"
            FILE_CSV "${PROJECT_SOURCE_DIR}/nodeset/euromap/Opc.Ua.PlasticsRubber.GeneralTypes.NodeSet2.csv"
            FILE_BSD "${PROJECT_SOURCE_DIR}/nodeset/euromap/Opc.Ua.PlasticsRubber.GeneralTypes.NodeSet2.bsd"
            IMPORT_BSD "UA_TYPES#${FILE_NS_DIRPREFIX}/Schema/Opc.Ua.Types.bsd"
            NAMESPACE_MAP "3:http://opcfoundation.org/UA/PlasticsRubber/GeneralTypes/"
            FILE_NS "${PROJECT_SOURCE_DIR}/nodeset/euromap/Opc.Ua.PlasticsRubber.GeneralTypes.NodeSet2.xml"
            DEPENDS "di"
            INTERNAL
    )

    # generate euromap77 namespace which is using euromap83
    ua_generate_nodeset_and_datatypes(
            NAME "euromap77"
            FILE_CSV "${PROJECT_SOURCE_DIR}/nodeset/euromap/Opc.Ua.PlasticsRubber.IMM2MES.NodeSet2.csv"
            FILE_BSD "${PROJECT_SOURCE_DIR}/nodeset/euromap/Opc.Ua.PlasticsRubber.IMM2MES.NodeSet2.bsd"
            IMPORT_BSD "UA_TYPES#${FILE_NS_DIRPREFIX}/Schema/Opc.Ua.Types.bsd"
            NAMESPACE_MAP "4:http://opcfoundation.org/UA/PlasticsRubber/IMM2MES/"
            FILE_NS "${PROJECT_SOURCE_DIR}/nodeset/euromap/Opc.Ua.PlasticsRubber.IMM2MES.NodeSet2.xml"
            DEPENDS "euromap83"
            INTERNAL
    )

    add_example(server_nodeset_euromap euromap/server_nodeset_euromap.c
            ${UA_NODESET_DI_SOURCES}
            ${UA_NODESET_EUROMAP83_SOURCES}
            ${UA_NODESET_EUROMAP77_SOURCES}
            ${UA_TYPES_DI_SOURCES}
            ${UA_TYPES_EUROMAP83_SOURCES}
            ${UA_TYPES_EUROMAP77_SOURCES}
            )
    add_dependencies(server_nodeset_euromap open62541-generator-ns-euromap77)

endif()
