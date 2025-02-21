#include <httplib.h>
#include <iostream>
#include <filesystem>
#include "video_processor.hpp"

namespace fs = std::filesystem;

int main() {
    httplib::Server server;
    VideoProcessor processor;
    
    // Create uploads directory if it doesn't exist
    fs::create_directories("uploads");
    fs::create_directories("processed");
    
    // Handle video upload and processing
    server.Post("/process", [&](const httplib::Request& req, httplib::Response& res) {
        std::cout << "\nReceived video upload request..." << std::endl;
        
        if (!req.has_file("video")) {
            std::cout << "Error: No video file in request" << std::endl;
            res.status = 400;
            res.set_content("No video file uploaded", "text/plain");
            return;
        }
        
        const auto& file = req.get_file_value("video");
        std::string input_path = "uploads/" + file.filename;
        std::string output_path = "processed/" + fs::path(file.filename).stem().string() + "_processed.mp4";
        
        std::cout << "Processing video: " << file.filename << std::endl;
        std::cout << "Input path: " << input_path << std::endl;
        std::cout << "Output path: " << output_path << std::endl;
        
        // Save uploaded file
        std::ofstream ofs(input_path, std::ios::binary);
        ofs.write(file.content.c_str(), file.content.size());
        ofs.close();
        
        // Process video
        if (processor.processVideo(input_path, output_path)) {
            std::cout << "Video processed successfully" << std::endl;
            res.set_content("Video processed successfully. Output: " + output_path, "text/plain");
        } else {
            std::cout << "Error processing video" << std::endl;
            res.status = 500;
            res.set_content("Error processing video", "text/plain");
        }
        
        // Clean up input file
        fs::remove(input_path);
    });
    
    // Serve processed videos
    server.Get("/processed/(.*)", [](const httplib::Request& req, httplib::Response& res) {
        std::string filepath = std::string("processed/") + req.matches[1].str();
        std::cout << "\nReceived request to download: " << filepath << std::endl;
        
        if (fs::exists(filepath)) {
            std::cout << "File found, starting download..." << std::endl;
            res.set_content_provider(
                fs::file_size(filepath),
                "video/mp4",
                [filepath](size_t offset, size_t length, httplib::DataSink& sink) {
                    std::ifstream file(filepath, std::ios::binary);
                    file.seekg(offset);
                    char buffer[4096];
                    size_t remaining = length;
                    while (remaining > 0 && !file.eof()) {
                        size_t batch_size = std::min(remaining, sizeof(buffer));
                        file.read(buffer, batch_size);
                        sink.write(buffer, file.gcount());
                        remaining -= file.gcount();
                    }
                    return true;
                }
            );
        } else {
            std::cout << "File not found: " << filepath << std::endl;
            res.status = 404;
        }
    });
    
    std::cout << "\n=== Video Processing Server ===" << std::endl;
    std::cout << "Server starting on port 8999..." << std::endl;
    std::cout << "Press Ctrl+C to stop the server" << std::endl;
    std::cout << "================================\n" << std::endl;
    
    server.listen("localhost", 8999);
    
    return 0;
} 