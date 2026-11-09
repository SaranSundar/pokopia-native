# pokopia-launch.exe: the game's launcher as a Windows program (launcher.c). The installer copies it into a build's out folder as
# "Play Pokopia <version>.exe" and "Pokopia Settings <version>.exe". Its icon is drawn at build time by make_icon.py (no image files in the source).
enable_language(RC)
find_package(Python3 COMPONENTS Interpreter)
set(_ico ${CMAKE_CURRENT_BINARY_DIR}/pokopia.ico)
if(Python3_Interpreter_FOUND)
  add_custom_command(OUTPUT ${_ico} COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/make_icon.py ${_ico}
                     DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/make_icon.py COMMENT "the launcher's icon")
  set(NXW_LAUNCH_ICON "1 ICON \"pokopia.ico\"")
else()
  message(STATUS "launcher: no Python, pokopia-launch.exe gets no icon")
  set(NXW_LAUNCH_ICON "")
endif()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/launcher.rc "#include <winver.h>
${NXW_LAUNCH_ICON}
1 VERSIONINFO
FILEVERSION 1,0,0,0
PRODUCTVERSION 1,0,0,0
FILEOS VOS_NT_WINDOWS32
FILETYPE VFT_APP
BEGIN
  BLOCK \"StringFileInfo\"
  BEGIN
    BLOCK \"040904b0\"
    BEGIN
      VALUE \"FileDescription\", \"Pokopia Native: play Pokopia built from your own dump\"
      VALUE \"ProductName\", \"Pokopia Native\"
      VALUE \"OriginalFilename\", \"pokopia-launch.exe\"
      VALUE \"LegalCopyright\", \"MIT licence, pokopia-native contributors\"
    END
  END
  BLOCK \"VarFileInfo\"
  BEGIN
    VALUE \"Translation\", 0x409, 1200
  END
END
")
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/launcher.rc PROPERTIES OBJECT_DEPENDS "${_ico}")
add_executable(pokopia-launch WIN32 launcher.c ${CMAKE_CURRENT_BINARY_DIR}/launcher.rc)
target_include_directories(pokopia-launch PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
if(Python3_Interpreter_FOUND)
  add_custom_target(pokopia-launch-icon DEPENDS ${_ico})
  add_dependencies(pokopia-launch pokopia-launch-icon)
  add_test(NAME launcher.play COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/test_launcher.py $<TARGET_FILE:pokopia-launch>)
endif()
