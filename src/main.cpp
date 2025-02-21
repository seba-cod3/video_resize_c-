#include "video_processor.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " <input_file> <output_file>" << std::endl;
        return 1;
    }
    
    VideoProcessor processor;
    if (processor.processVideo(argv[1], argv[2])) {
        std::cout << "Video processed successfully" << std::endl;
        return 0;
    }
    
    std::cout << "Error processing video" << std::endl;
    return 1;
}