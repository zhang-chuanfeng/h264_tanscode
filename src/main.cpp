#include <iostream>
#include <iomanip>
#include "transcoder.h"

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "用法: " << argv[0] << " <输入文件> <输出文件>" << std::endl;
        return 1;
    }

    Transcoder transcoder;
    TranscoderParams params;
    params.bitrate = 2000000;  // 2 Mbps
    params.profile = FF_PROFILE_H264_MAIN;
    params.level = 41;  // Level 4.1

    // 设置进度回调
    auto progress_callback = [](float progress) {
        static int last_percentage = -1;
        static bool printed_100 = false;
        int current_percentage = static_cast<int>(progress);
        
        // 确保进度在有效范围内
        progress = std::min(100.0f, std::max(0.0f, progress));
        
        // 修改判断条件，确保显示所有进度包括100%
        if (current_percentage != last_percentage || 
            (progress > 99.9f && !printed_100)) {
            
            last_percentage = current_percentage;
            
            // 清除当前行
            std::cout << "\r";
            
            // 进度条宽度为50个字符
            const int bar_width = 50;
            int pos = bar_width * progress / 100.0f;
            
            // 打印进度条
            std::cout << "[";
            for (int i = 0; i < bar_width; ++i) {
                if (i < pos) std::cout << "=";
                else if (i == pos && progress < 99.9f) std::cout << ">";
                else if (i == pos && progress >= 99.9f) std::cout << "=";
                else std::cout << " ";
            }
            
            // 打印百分比
            std::cout << "] " << std::fixed << std::setprecision(1);
            if (progress > 99.9f) {
                std::cout << "100.0";
                printed_100 = true;
            } else {
                std::cout << progress;
            }
            std::cout << "%" << std::flush;
        }
    };

    bool success = transcoder.transcode(argv[1], argv[2], params, progress_callback);
    std::cout << std::endl;

    if (!success) {
        std::cout << "转码失败: " << transcoder.getLastError() << std::endl;
        return 1;
    }
    std::cout << "转码完成!" << std::endl;
    return 0;
}
