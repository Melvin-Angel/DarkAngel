# Acquired later packages are deliberately not included by the M0 project.
# Include this in the focused M2 renderer spike, after find_package(ZLIB/PNG).
function(darkangel_add_diligent)
  set(DILIGENT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(DILIGENT_NO_DIRECT3D11 ON CACHE BOOL "" FORCE)
  set(DILIGENT_NO_DIRECT3D12 OFF CACHE BOOL "" FORCE)
  set(DILIGENT_NO_OPENGL ON CACHE BOOL "" FORCE)
  set(DILIGENT_NO_METAL ON CACHE BOOL "" FORCE)
  set(DILIGENT_NO_WEBGPU ON CACHE BOOL "" FORCE)
  set(DILIGENT_NO_ARCHIVER ON CACHE BOOL "" FORCE)
  set(DILIGENT_NO_SUPER_RESOLUTION ON CACHE BOOL "" FORCE)
  set(DILIGENT_NO_RENDER_STATE_PACKAGER ON CACHE BOOL "" FORCE)
  set(DILIGENT_INSTALL_TOOLS OFF CACHE BOOL "" FORCE)
  set(DILIGENT_ENABLE_DRACO OFF CACHE BOOL "" FORCE)
  set(DILIGENT_DEAR_IMGUI_PATH "${PROJECT_SOURCE_DIR}/third_party/imgui-docking" CACHE PATH "" FORCE)
  find_package(ZLIB REQUIRED)
  find_package(PNG REQUIRED)
  add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/diligent-core" "${PROJECT_BINARY_DIR}/diligent-core" EXCLUDE_FROM_ALL)
  add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/diligent-tools" "${PROJECT_BINARY_DIR}/diligent-tools" EXCLUDE_FROM_ALL)
  # Diligent-Imgui compiles the only active Dear ImGui copy. ImGuizmo must link
  # this provider rather than using the vcpkg port, which depends on another imgui.
endfunction()
