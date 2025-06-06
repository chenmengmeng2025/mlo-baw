#include <iostream>
#include <vector>
#include <omp.h>
#include <filesystem>
#include <regex>
#include <cstdlib>
#include <stdexcept>
namespace fs = std::filesystem;

void safe_print(const std::string& message) {
    #pragma omp critical
    {
        std::cout << message << std::endl;
    }
}

int main() {
    const std::string folder_path = "./scratch/tcp-params";
    std::regex json_file_regex(R"(.*?(\d+)\.json$)");
    std::smatch match;
    std::vector<int> param_ids;
    std::vector<std::string> param_paths;
    // 1. 查找所有匹配的JSON文件并提取参数ID
    try {
        if (!fs::exists(folder_path) || !fs::is_directory(folder_path)) {
            throw std::runtime_error(folder_path + " is not a valid directory");
        }

        for (const auto& entry : fs::directory_iterator(folder_path)) {
            if (entry.is_regular_file()) {
                std::string file_name = entry.path().filename().string();
                param_paths.push_back(entry.path().string());
                if (std::regex_match(file_name, match, json_file_regex)) {
                    int id = std::stoi(match[1]);
                    param_ids.push_back(id);
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    // 检查是否找到有效参数ID
    if (param_ids.empty()) {
        std::cerr << "No valid JSON files found in directory: " << folder_path << std::endl;
        return EXIT_FAILURE;
    }

    // 2. 创建线程运行命令
    const int hardware_cores = std::min(omp_get_max_threads(), 4); // 限制最大4线程
    omp_set_num_threads(hardware_cores);
    
    safe_print("\nStarting parameter scan with " + std::to_string(hardware_cores) + 
               " threads on " + std::to_string(param_ids.size()) + " parameter sets\n");
    
    #pragma omp parallel for schedule(dynamic)
    for (size_t i = 0; i < param_ids.size(); i++) {
        const int paramset_id = param_ids[i];
        
        // 构建ns-3命令
        const std::string command = "./ns3 run mode-test-tcp-grid-search-pp -- --paramsetid=" + 
                                   std::to_string(paramset_id) + "--interference=0 --param_update=0 --redundancy=1 --simt=3 --mode=1 --paramsfile=" + param_paths[i];
        
        safe_print(command);

        // 执行命令
        const int exit_code = std::system(command.c_str());
        
        // 检查结果
        if (exit_code == 0) {
            safe_print("Thread " + std::to_string(omp_get_thread_num()) + 
                      ": SUCCESS - Paramset ID " + std::to_string(paramset_id));
        } else {
            safe_print("Thread " + std::to_string(omp_get_thread_num()) + 
                      ": FAILED (" + std::to_string(exit_code) + 
                      ") - Paramset ID " + std::to_string(paramset_id));
        }
    }
    
    return 0;
}