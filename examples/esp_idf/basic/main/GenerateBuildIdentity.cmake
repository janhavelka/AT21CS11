cmake_minimum_required(VERSION 3.22)

file(REAL_PATH "${AT21CS_SOURCE_DIR}" source_dir)
set(commit "unknown")
set(status "unknown")

find_package(Git QUIET)
if(Git_FOUND)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" rev-parse --show-toplevel
    RESULT_VARIABLE root_result OUTPUT_VARIABLE top_level
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
  )
  if(root_result EQUAL 0)
    file(REAL_PATH "${top_level}" top_level)
    if(WIN32)
      string(TOLOWER "${source_dir}" source_dir)
      string(TOLOWER "${top_level}" top_level)
    endif()
    # A copy inside another repository must not advertise the parent's commit.
    if(top_level STREQUAL source_dir)
      execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" rev-parse --short=12 HEAD
        RESULT_VARIABLE commit_result OUTPUT_VARIABLE git_commit
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
      )
      execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" status --porcelain --untracked-files=normal
        RESULT_VARIABLE status_result OUTPUT_VARIABLE changes
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
      )
      if(commit_result EQUAL 0 AND status_result EQUAL 0 AND git_commit MATCHES "^[0-9a-f]+$")
        set(commit "${git_commit}")
        if(changes STREQUAL "")
          set(status "clean")
        else()
          set(status "dirty")
        endif()
      endif()
    endif()
  endif()
endif()

# file(CONFIGURE) preserves the timestamp when the identity is unchanged.
file(CONFIGURE OUTPUT "${AT21CS_IDENTITY_HEADER}" CONTENT [=[#pragma once
#define AT21CS_GIT_COMMIT "@commit@"
#define AT21CS_GIT_STATUS "@status@"
]=] @ONLY NEWLINE_STYLE LF)
