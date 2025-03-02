IF (HOST_OS_DARWIN AND HOST_ARCH_X86_64)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL_HOST sbr:2914538088)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL3_HOST sbr:2914550081)
ELSEIF (HOST_OS_DARWIN AND HOST_ARCH_ARM64)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL_HOST sbr:2914537169)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL3_HOST sbr:2914549263)
ELSEIF (HOST_OS_LINUX AND HOST_ARCH_PPC64LE)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL_HOST sbr:2914537819)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL3_HOST sbr:2914549721)
ELSEIF (HOST_OS_LINUX AND HOST_ARCH_X86_64)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL_HOST sbr:2914538586)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL3_HOST sbr:2914550857)
ELSEIF (HOST_OS_WINDOWS AND HOST_ARCH_X86_64)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL_HOST sbr:2914536581)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL3_HOST sbr:2914549015)
ENDIF()
