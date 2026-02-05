function(_rollnw_compute_sanitizers out_var)
  set(LIST_OF_SANITIZERS "")

  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
    set(SANITIZERS "")

    option(ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" FALSE)
    if(ENABLE_SANITIZER_ADDRESS)
      list(APPEND SANITIZERS "address")
    endif()

    option(ENABLE_SANITIZER_LEAK "Enable leak sanitizer" FALSE)
    if(ENABLE_SANITIZER_LEAK)
      list(APPEND SANITIZERS "leak")
    endif()

    option(ENABLE_SANITIZER_UNDEFINED_BEHAVIOR "Enable undefined behavior sanitizer" FALSE)
    if(ENABLE_SANITIZER_UNDEFINED_BEHAVIOR)
      list(APPEND SANITIZERS "undefined")
    endif()

    option(ENABLE_SANITIZER_THREAD "Enable thread sanitizer" FALSE)
    if(ENABLE_SANITIZER_THREAD)
      if("address" IN_LIST SANITIZERS OR "leak" IN_LIST SANITIZERS)
        message(WARNING "Thread sanitizer does not work with Address and Leak sanitizer enabled")
      else()
        list(APPEND SANITIZERS "thread")
      endif()
    endif()

    option(ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" FALSE)
    if(ENABLE_SANITIZER_MEMORY AND CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
      if("address" IN_LIST SANITIZERS
         OR "thread" IN_LIST SANITIZERS
         OR "leak" IN_LIST SANITIZERS)
        message(WARNING "Memory sanitizer does not work with Address, Thread and Leak sanitizer enabled")
      else()
        list(APPEND SANITIZERS "memory")
      endif()
    endif()

    list(JOIN SANITIZERS "," LIST_OF_SANITIZERS)
  endif()

  set(${out_var} "${LIST_OF_SANITIZERS}" PARENT_SCOPE)
endfunction()

function(enable_global_sanitizers)
  _rollnw_compute_sanitizers(LIST_OF_SANITIZERS)
  if(NOT LIST_OF_SANITIZERS OR "${LIST_OF_SANITIZERS}" STREQUAL "")
    return()
  endif()

  # Must be called early (top-level) so external deps are instrumented too.
  add_compile_options(-fsanitize=${LIST_OF_SANITIZERS})
  add_link_options(-fsanitize=${LIST_OF_SANITIZERS})
  set(ROLLNW_GLOBAL_SANITIZERS "${LIST_OF_SANITIZERS}" CACHE INTERNAL "")
endfunction()

function(enable_sanitizers project_name)
  if(DEFINED ROLLNW_GLOBAL_SANITIZERS AND NOT "${ROLLNW_GLOBAL_SANITIZERS}" STREQUAL "")
    return()
  endif()

  _rollnw_compute_sanitizers(LIST_OF_SANITIZERS)
  if(NOT LIST_OF_SANITIZERS OR "${LIST_OF_SANITIZERS}" STREQUAL "")
    return()
  endif()

  # Use PUBLIC so the target itself is instrumented and dependents link
  # against the sanitizer runtime (static libs need this propagated).
  target_compile_options(${project_name} PUBLIC -fsanitize=${LIST_OF_SANITIZERS})
  target_link_options(${project_name} PUBLIC -fsanitize=${LIST_OF_SANITIZERS})
endfunction()
