ez_cmake_init()

ez_build_filter_runtime()

ez_requires(EZ_CMAKE_PLATFORM_WINDOWS_DESKTOP)

ez_requires(EZ_3RDPARTY_RENDERDOC_SUPPORT)

# Get the name of this folder as the project name
get_filename_component(PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME_WE)

ez_create_target(SHARED_LIBRARY ${PROJECT_NAME})

target_compile_definitions(${PROJECT_NAME} PUBLIC BUILDSYSTEM_ENABLE_RENDERDOC_SUPPORT)

target_link_libraries(${PROJECT_NAME}
  PUBLIC
  Core
)
