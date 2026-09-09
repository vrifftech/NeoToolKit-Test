include(FetchContent)
option(NEOTOOLKIT_USE_SIBLINGS "Use local sibling repositories when present" ON)
# CI provides one freshly resolved source snapshot. Local collection builds use
# their sibling sources without rewriting developer checkouts. An isolated host
# obtains the latest main branch automatically; old *_REF variables are ignored.
function(neo_component key folder default_repository required_api)
    set(rootvar "NEOTOOLKIT_${key}_SOURCE_DIR")
    if(NEOTOOLKIT_USE_SIBLINGS AND EXISTS "${PROJECT_SOURCE_DIR}/../${folder}/CMakeLists.txt")
        set(resolved "${PROJECT_SOURCE_DIR}/../${folder}")
    elseif(NEOTOOLKIT_USE_SIBLINGS AND key STREQUAL "NEOSHARED" AND EXISTS "${PROJECT_SOURCE_DIR}/../NeoShared/CMakeLists.txt")
        set(resolved "${PROJECT_SOURCE_DIR}/../NeoShared")
    else()
        string(TOLOWER "neotoolkit_${key}" content)
        FetchContent_Declare(${content}
            GIT_REPOSITORY "${default_repository}" GIT_TAG origin/main
            GIT_SHALLOW TRUE GIT_SUBMODULES "" SOURCE_SUBDIR "__fetch_only__")
        FetchContent_MakeAvailable(${content})
        set(resolved "${${content}_SOURCE_DIR}")
    endif()
    get_filename_component(resolved "${resolved}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    if(NOT EXISTS "${resolved}/${required_api}")
        message(FATAL_ERROR "${folder} is missing ${required_api}. Publish the complete current source to ${default_repository}; this build selects main automatically.")
    endif()
    set(${rootvar} "${resolved}" PARENT_SCOPE)
endfunction()
