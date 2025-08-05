#include <unordered_set>
#include <unordered_map>
#include <set>
#include <vector>
#include <iostream>
#include <algorithm>
#include <chrono>

// 方案1：基于元素到集合ID的映射 + 并查集
class AutoMergeSets {
private:
    std::vector<std::set<int>> sets_;           // 存储所有集合
    std::unordered_map<int, int> element_to_set_; // 元素到集合ID的映射
    std::vector<int> parent_;                   // 并查集父节点
    std::vector<bool> valid_;                   // 集合是否有效
    int next_id_;

    // 并查集查找根节点（带路径压缩）
    int find(int x) {
        if (parent_[x] != x) {
            parent_[x] = find(parent_[x]);
        }
        return parent_[x];
    }

    // 合并两个集合
    void union_sets(int a, int b) {
        int root_a = find(a);
        int root_b = find(b);
        
        if (root_a != root_b) {
            // 将较小的集合合并到较大的集合中
            if (sets_[root_a].size() < sets_[root_b].size()) {
                std::swap(root_a, root_b);
            }
            
            // 合并集合
            sets_[root_a].insert(sets_[root_b].begin(), sets_[root_b].end());
            
            // 更新元素映射
            for (int elem : sets_[root_b]) {
                element_to_set_[elem] = root_a;
            }
            
            // 更新并查集
            parent_[root_b] = root_a;
            valid_[root_b] = false;
            sets_[root_b].clear();
        }
    }

public:
    AutoMergeSets() : next_id_(0) {}

    // 插入新集合，自动合并有交集的集合
    void insert(const std::set<int>& new_set) {
        if (new_set.empty()) return;

        std::vector<int> intersecting_sets;
        
        // 找到所有与新集合有交集的现有集合
        for (int elem : new_set) {
            auto it = element_to_set_.find(elem);
            if (it != element_to_set_.end()) {
                int set_id = find(it->second);
                if (valid_[set_id]) {
                    intersecting_sets.push_back(set_id);
                }
            }
        }
        
        // 去重
        std::sort(intersecting_sets.begin(), intersecting_sets.end());
        intersecting_sets.erase(
            std::unique(intersecting_sets.begin(), intersecting_sets.end()),
            intersecting_sets.end()
        );

        if (intersecting_sets.empty()) {
            // 没有交集，创建新集合
            int new_id = next_id_++;
            sets_.resize(next_id_);
            parent_.resize(next_id_);
            valid_.resize(next_id_);
            
            sets_[new_id] = new_set;
            parent_[new_id] = new_id;
            valid_[new_id] = true;
            
            for (int elem : new_set) {
                element_to_set_[elem] = new_id;
            }
        } else {
            // 有交集，合并所有相关集合
            int target_set = intersecting_sets[0];
            
            // 将新集合的元素加入目标集合
            sets_[target_set].insert(new_set.begin(), new_set.end());
            for (int elem : new_set) {
                element_to_set_[elem] = target_set;
            }
            
            // 合并所有有交集的集合
            for (size_t i = 1; i < intersecting_sets.size(); ++i) {
                union_sets(target_set, intersecting_sets[i]);
            }
        }
    }

    // 获取所有有效的集合
    std::vector<std::set<int>> get_all_sets() const {
        std::vector<std::set<int>> result;
        for (int i = 0; i < next_id_; ++i) {
            if (valid_[i] && find(i) == i) {
                result.push_back(sets_[i]);
            }
        }
        return result;
    }

    // 查找包含指定元素的集合
    std::set<int> find_set_containing(int elem) const {
        auto it = element_to_set_.find(elem);
        if (it != element_to_set_.end()) {
            int set_id = find(const_cast<AutoMergeSets*>(this)->find(it->second));
            if (valid_[set_id]) {
                return sets_[set_id];
            }
        }
        return {};
    }

    // 获取集合数量
    size_t size() const {
        size_t count = 0;
        for (int i = 0; i < next_id_; ++i) {
            if (valid_[i] && find(i) == i) {
                count++;
            }
        }
        return count;
    }

    // 打印所有集合
    void print_all() const {
        auto all_sets = get_all_sets();
        std::cout << "当前有 " << all_sets.size() << " 个集合:" << std::endl;
        for (size_t i = 0; i < all_sets.size(); ++i) {
            std::cout << "集合 " << i + 1 << ": { ";
            for (int elem : all_sets[i]) {
                std::cout << elem << " ";
            }
            std::cout << "}" << std::endl;
        }
    }
};

// 方案2：优化版本 - 使用哈希表加速交集检测
class FastAutoMergeSets {
private:
    std::vector<std::unordered_set<int>> sets_;
    std::unordered_map<int, std::unordered_set<int>> element_to_sets_; // 元素到包含它的集合ID列表
    std::vector<bool> valid_;
    int next_id_;

public:
    FastAutoMergeSets() : next_id_(0) {}

    void insert(const std::set<int>& new_set) {
        if (new_set.empty()) return;

        std::unordered_set<int> sets_to_merge;
        
        // 快速找到所有与新集合有交集的集合
        for (int elem : new_set) {
            if (element_to_sets_.count(elem)) {
                for (int set_id : element_to_sets_[elem]) {
                    if (valid_[set_id]) {
                        sets_to_merge.insert(set_id);
                    }
                }
            }
        }

        if (sets_to_merge.empty()) {
            // 创建新集合
            int new_id = next_id_++;
            sets_.resize(next_id_);
            valid_.resize(next_id_);
            
            sets_[new_id] = std::unordered_set<int>(new_set.begin(), new_set.end());
            valid_[new_id] = true;
            
            for (int elem : new_set) {
                element_to_sets_[elem].insert(new_id);
            }
        } else {
            // 选择第一个集合作为目标，合并所有其他集合
            auto it = sets_to_merge.begin();
            int target_id = *it++;
            
            // 添加新集合的元素
            for (int elem : new_set) {
                if (sets_[target_id].insert(elem).second) {
                    element_to_sets_[elem].insert(target_id);
                }
            }
            
            // 合并其他集合
            for (; it != sets_to_merge.end(); ++it) {
                int merge_id = *it;
                if (merge_id != target_id && valid_[merge_id]) {
                    // 将 merge_id 的所有元素移动到 target_id
                    for (int elem : sets_[merge_id]) {
                        if (sets_[target_id].insert(elem).second) {
                            element_to_sets_[elem].erase(merge_id);
                            element_to_sets_[elem].insert(target_id);
                        } else {
                            element_to_sets_[elem].erase(merge_id);
                        }
                    }
                    sets_[merge_id].clear();
                    valid_[merge_id] = false;
                }
            }
        }
    }

    std::vector<std::set<int>> get_all_sets() const {
        std::vector<std::set<int>> result;
        for (int i = 0; i < next_id_; ++i) {
            if (valid_[i]) {
                result.emplace_back(sets_[i].begin(), sets_[i].end());
            }
        }
        return result;
    }

    size_t size() const {
        size_t count = 0;
        for (int i = 0; i < next_id_; ++i) {
            if (valid_[i]) count++;
        }
        return count;
    }
};

// 性能测试和示例
int main() {
    std::cout << "=== 自动合并集合演示 ===" << std::endl;
    
    AutoMergeSets merger;
    
    // 测试用例
    std::vector<std::set<int>> test_sets = {
        {1, 2, 3},     // 集合1
        {4, 5, 6},     // 集合2（独立）
        {3, 7, 8},     // 集合3（与集合1有交集，应该合并）
        {9, 10},       // 集合4（独立）
        {6, 11, 12},   // 集合5（与集合2有交集，应该合并）
        {8, 13},       // 集合6（与已合并的集合1+3有交集）
        {14, 15},      // 集合7（独立）
        {10, 16}       // 集合8（与集合4有交集）
    };

    std::cout << "插入过程：" << std::endl;
    for (size_t i = 0; i < test_sets.size(); ++i) {
        std::cout << "\n插入集合 " << i + 1 << ": { ";
        for (int x : test_sets[i]) {
            std::cout << x << " ";
        }
        std::cout << "}" << std::endl;
        
        merger.insert(test_sets[i]);
        merger.print_all();
    }

    std::cout << "\n=== 查找测试 ===" << std::endl;
    std::vector<int> test_elements = {1, 5, 8, 10, 14, 20};
    for (int elem : test_elements) {
        auto found_set = merger.find_set_containing(elem);
        std::cout << "元素 " << elem << " 属于集合: { ";
        for (int x : found_set) {
            std::cout << x << " ";
        }
        std::cout << "}" << std::endl;
    }

    // 性能测试
    std::cout << "\n=== 性能测试 ===" << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    FastAutoMergeSets fast_merger;
    
    // 生成大量测试数据
    for (int i = 0; i < 1000; ++i) {
        std::set<int> random_set;
        for (int j = 0; j < 5; ++j) {
            random_set.insert(rand() % 500); // 有重叠的可能性
        }
        fast_merger.insert(random_set);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "插入1000个随机集合耗时: " << duration.count() << "ms" << std::endl;
    std::cout << "最终集合数量: " << fast_merger.size() << std::endl;

    return 0;
}