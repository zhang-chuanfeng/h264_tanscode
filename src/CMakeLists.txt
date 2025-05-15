# 收集源文件
file(GLOB SOURCES "*.cpp")

# 创建可执行文件
add_executable(test_app ${SOURCES})

# 链接FFmpeg库
target_link_libraries(test_app ${FFMPEG_LIBRARIES})
