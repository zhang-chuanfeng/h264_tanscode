# H264 Transcode

这是一个基于C++的H264视频转码工具，可以对视频文件进行格式转换和处理。

## 功能特点

- 支持H264视频格式的转码
- 基于CMake构建系统
- 简单易用的接口

## 环境要求

- C++11或更高版本
- CMake 3.0或更高版本
- FFmpeg开发库

## 编译安装

1. 克隆仓库
```bash
git clone https://github.com/你的用户名/h264_transcode.git
cd h264_transcode
```

2. 创建构建目录并编译
```bash
mkdir build
cd build
cmake ..
make
```

## 使用方法

程序接受输入视频文件路径作为参数：

```bash
./build/src/test_app 输入视频路径
```

## 许可证

MIT License

## 贡献

欢迎提交Issue和Pull Request！
