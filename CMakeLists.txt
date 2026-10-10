cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(embedded_sim)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(app PRIVATE main.cpp core/registry.cpp core/rt/runner.cpp net/wifi.cpp)
target_sources_ifdef(CONFIG_SIM_TELEMETRY app PRIVATE net/telemetry.cpp net/mqtt/payload.cpp)
target_sources_ifdef(CONFIG_SIM_MQTT app PRIVATE net/mqtt/client.cpp)

if(CONFIG_SIM_WEBSERVER)
  # Frontend: web/dist (scripts/web-build.sh) if built, else the tiny web/fallback page.
  set(web_dir ${CMAKE_CURRENT_SOURCE_DIR}/web/dist)
  if(NOT EXISTS ${web_dir}/index.html)
    message(WARNING "web/dist not built (run scripts/web-build.sh): embedding web/fallback")
    set(web_dir ${CMAKE_CURRENT_SOURCE_DIR}/web/fallback)
  endif()
  foreach(f index.html app.js app.css)
    generate_inc_file_for_target(app ${web_dir}/${f}
      ${ZEPHYR_BINARY_DIR}/include/generated/${f}.gz.inc --gzip)
  endforeach()
  target_sources(app PRIVATE net/ws/service.c net/ws/ws.cpp)
  zephyr_linker_sources(SECTIONS net/ws/sections-rom.ld)
endif()

add_subdirectory(sims)
