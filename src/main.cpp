#include "../include/DancingMatrix.h"

bool runWithTimeout(std::function<void()> func, int timeoutSeconds) {
    std::packaged_task<void()> task(func);
    std::future<void> future = task.get_future();

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8 * 1024 * 1024); // 8 MB 栈空间

    pthread_t tid;
    auto heapTask = new std::packaged_task<void()>(std::move(task));

    pthread_create(&tid, &attr, [](void* arg) -> void* {
        std::unique_ptr<std::packaged_task<void()>> taskPtr(static_cast<std::packaged_task<void()>*>(arg));
        (*taskPtr)();
        return nullptr;
    }, heapTask);

    pthread_attr_destroy(&attr);

    if (future.wait_for(std::chrono::seconds(timeoutSeconds)) == std::future_status::timeout) {
        return false;
    }

    pthread_join(tid, nullptr);
    return true;
}


int main(){
    try
    {
        // 文件夹路径
        const std::string folderPath1 = "../data/exact_cover_benchmark";
        const std::string folderPath2 = "../data/set_partitioning_benchmarks";
    
        // std::cout << "exact_cover_benchmark 文件夹处理开始" << std::endl;
        //     // 遍历文件夹
        // for (const auto& entry : fs::directory_iterator(folderPath1))
        // {
        //     if (entry.is_regular_file() && entry.path().extension() == ".txt")
        //     {
        //         // 文件路径
        //         const std::string filePath = entry.path().string();
        //         std::cout << "文件名: " << entry.path().filename().string() << std::endl;

        //         // 从文件中提取 n 和 m 的值
        //         int r, c;
        //         int** matrix = PreProccess::processFileToMatrix1(filePath, r, c);

        //         // 创建 DancingMatrix 对象
        //         DancingMatrix dmx(r, c, matrix);

                
        //         std::cout << "开始搜索..." << std::endl;    
        //         dmx.startSearch(false);

        //         // 释放内存
        //         PreProccess::freeMatrix(matrix, r);
        //     }
        // }
        // std::cout << "exact_cover_benchmark 文件夹处理完毕" << std::endl;

        std::cout << "set_partitioning_benchmarks 文件夹处理开始" << std::endl;
        // 遍历文件夹
        for (const auto& entry : fs::directory_iterator(folderPath2))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".txt")
            {
                // 文件路径
                std::cout << "文件名: " << entry.path().filename().string() << std::endl;

                // 从文件中提取 n 和 m 的值
                int r, c;
                int** matrix = PreProccess::processFileToMatrix2(entry.path(), r, c);

                // 创建 DancingMatrix 对象
                DancingMatrix dmx(r, c, matrix);

                std::cout << "开始搜索..." << std::endl;
                dmx.startSearch();
                

                // 释放内存
                PreProccess::freeMatrix(matrix, r);
            }
        }
        std::cout << "set_partitioning_benchmarks 文件夹处理完毕" << std::endl;
    }
    catch(const std::exception& e)
    {
        std::cerr << "错误：" << e.what() << '\n';
    }
    
    return 0;
}