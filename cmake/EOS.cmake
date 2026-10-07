# Opt-in for M3. M0 never links or stages EOS.
function(darkangel_import_eos sdk_root)
  foreach(file Include/eos_sdk.h Include/eos_version.h Lib/EOSSDK-Win64-Shipping.lib Bin/EOSSDK-Win64-Shipping.dll)
    if(NOT EXISTS "${sdk_root}/${file}")
      message(FATAL_ERROR "Missing authorized EOS Windows C SDK input: ${sdk_root}/${file}")
    endif()
  endforeach()
  if(NOT TARGET DarkAngel::EOS)
    add_library(DarkAngel::EOS SHARED IMPORTED GLOBAL)
    set_target_properties(DarkAngel::EOS PROPERTIES
      IMPORTED_IMPLIB "${sdk_root}/Lib/EOSSDK-Win64-Shipping.lib"
      IMPORTED_LOCATION "${sdk_root}/Bin/EOSSDK-Win64-Shipping.dll"
      INTERFACE_INCLUDE_DIRECTORIES "${sdk_root}/Include")
  endif()
endfunction()
