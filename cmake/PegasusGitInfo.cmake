execute_process(COMMAND
    git
    --git-dir "${PROJECT_SOURCE_DIR}/.git"
    --work-tree "${PROJECT_SOURCE_DIR}"
    describe
    --always
    --dirty
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE PEGASUS_GIT_DESCRIBE
)

# PoniaEmu's own version starts at 1.0.0 and is shown as "1.0.0+commit", with -dirty added when there are uncommitted changes
# PONIA_VERSION comes from cmake/PoniaVersion.cmake (the product name and the version are written in one place only)
execute_process(COMMAND
    git
    --git-dir "${PROJECT_SOURCE_DIR}/.git"
    --work-tree "${PROJECT_SOURCE_DIR}"
    rev-parse
    --short=8 HEAD
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE PONIA_GIT_SHORT
)
string(FIND "${PEGASUS_GIT_DESCRIBE}" "-dirty" PONIA_DIRTY_POS)
if(PONIA_DIRTY_POS GREATER -1)
    set(PONIA_DIRTY_SUFFIX "-dirty")
else()
    set(PONIA_DIRTY_SUFFIX "")
endif()
if(PONIA_GIT_SHORT)
    set(PEGASUS_GIT_REVISION "${PONIA_VERSION}+${PONIA_GIT_SHORT}${PONIA_DIRTY_SUFFIX}")
else()
    set(PEGASUS_GIT_REVISION "${PONIA_VERSION}")
endif()

execute_process(COMMAND
    git
    --git-dir "${PROJECT_SOURCE_DIR}/.git"
    --work-tree "${PROJECT_SOURCE_DIR}"
    log -1
    --format=%cd
    --date=short
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE PEGASUS_GIT_DATE
)

execute_process(COMMAND
    git
    --git-dir "${PROJECT_SOURCE_DIR}/.git"
    --work-tree "${PROJECT_SOURCE_DIR}"
    rev-list
    --count HEAD
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE PEGASUS_GIT_COMMIT_CNT
)
