set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# This toolchain enables Linux -> Windows x64 cross-compilation with LLVM tools.
# It expects an xwin "splat" layout containing both CRT and Windows SDK files.
if(NOT DEFINED MINECRAFTCONSOLES_WINSDK_ROOT)
  if(DEFINED ENV{MINECRAFTCONSOLES_WINSDK_ROOT})
    set(MINECRAFTCONSOLES_WINSDK_ROOT "$ENV{MINECRAFTCONSOLES_WINSDK_ROOT}")
  elseif(EXISTS "/opt/xwin")
    set(MINECRAFTCONSOLES_WINSDK_ROOT "/opt/xwin")
  endif()
endif()

if(NOT MINECRAFTCONSOLES_WINSDK_ROOT)
  message(FATAL_ERROR
    "Linux cross-compile requires MINECRAFTCONSOLES_WINSDK_ROOT.\n"
    "Point it to an xwin splat folder, for example: /opt/xwin"
  )
endif()

set(_winsdk_crt_include "${MINECRAFTCONSOLES_WINSDK_ROOT}/crt/include")
set(_winsdk_crt_lib "${MINECRAFTCONSOLES_WINSDK_ROOT}/crt/lib/x86_64")
set(_winsdk_sdk_include_ucrt "${MINECRAFTCONSOLES_WINSDK_ROOT}/sdk/include/ucrt")
set(_winsdk_sdk_include_um "${MINECRAFTCONSOLES_WINSDK_ROOT}/sdk/include/um")
set(_winsdk_sdk_include_shared "${MINECRAFTCONSOLES_WINSDK_ROOT}/sdk/include/shared")
set(_winsdk_sdk_include_winrt "${MINECRAFTCONSOLES_WINSDK_ROOT}/sdk/include/winrt")
set(_winsdk_sdk_lib_ucrt "${MINECRAFTCONSOLES_WINSDK_ROOT}/sdk/lib/ucrt/x86_64")
set(_winsdk_sdk_lib_um "${MINECRAFTCONSOLES_WINSDK_ROOT}/sdk/lib/um/x86_64")

foreach(_required_path
  "${_winsdk_crt_include}"
  "${_winsdk_crt_lib}"
  "${_winsdk_sdk_include_ucrt}"
  "${_winsdk_sdk_include_um}"
  "${_winsdk_sdk_include_shared}"
  "${_winsdk_sdk_include_winrt}"
  "${_winsdk_sdk_lib_ucrt}"
  "${_winsdk_sdk_lib_um}"
)
  if(NOT EXISTS "${_required_path}")
    message(FATAL_ERROR
      "Missing required Windows SDK/CRT path: ${_required_path}\n"
      "Check MINECRAFTCONSOLES_WINSDK_ROOT and ensure xwin splat output is complete."
    )
  endif()
endforeach()

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER lld-link)
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_ASM_MASM_COMPILER llvm-ml)

set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_ASM_MASM_FLAGS_INIT "--mx86-64")

# Avoid configure checks that require linking a runnable host executable.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(_common_include_flags
  "/imsvc${_winsdk_crt_include}"
  "/imsvc${_winsdk_sdk_include_ucrt}"
  "/imsvc${_winsdk_sdk_include_um}"
  "/imsvc${_winsdk_sdk_include_shared}"
  "/imsvc${_winsdk_sdk_include_winrt}"
)

set(_common_link_flags
  "/libpath:${_winsdk_crt_lib}"
  "/libpath:${_winsdk_sdk_lib_ucrt}"
  "/libpath:${_winsdk_sdk_lib_um}"
)

string(JOIN " " _common_include_flags_joined ${_common_include_flags})
string(JOIN " " _common_link_flags_joined ${_common_link_flags})

set(CMAKE_C_FLAGS_INIT "${_common_include_flags_joined}")
set(CMAKE_CXX_FLAGS_INIT "${_common_include_flags_joined}")
set(CMAKE_RC_FLAGS_INIT
  "/I${_winsdk_crt_include} /I${_winsdk_sdk_include_ucrt} /I${_winsdk_sdk_include_um} /I${_winsdk_sdk_include_shared} /I${_winsdk_sdk_include_winrt}"
)
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_common_link_flags_joined}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_common_link_flags_joined}")
