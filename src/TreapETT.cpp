#include "../include/TreapETT.h"

// 缓存（增量模式）
thread_local unordered_map<ETTreapNode *, Block> cached_root_blocks;
thread_local unordered_map<int, ETTreapNode *> cached_row_root;
thread_local unordered_set<int> cached_block_rows;

std::vector<Block> ETTree::findComponentsInBlock(const std::unordered_set<int> &block_rows)
{
        vector<int> added, removed;
        for (int r : block_rows) if (!cached_block_rows.count(r)) added.push_back(r);
        for (int r : cached_block_rows) if (!block_rows.count(r)) removed.push_back(r);

        // bool small_delta = (added.size() + removed.size()) * 5 < block_rows.size();

        // std::shared_lock lock(mutex_);  // 读锁
        // if (!small_delta) {
        //     cached_root_blocks.clear();
        //     cached_row_root.clear();
        //     cached_block_rows = block_rows;

        //     for (int row : block_rows) {
        //         if (!isRowActive(row)) continue;
        //         auto it = vertex_repr.find(row);
        //         if (it == vertex_repr.end()) continue;
        //         auto *root = treap_get_root(it->second);

        //         Block &blk = cached_root_blocks[root];
        //         blk.rows.insert(row);
        //         if (get_row_cols) {
        //             const auto &cols = get_row_cols(row);
        //             blk.cols.insert(cols.begin(), cols.end());
        //         }
        //         cached_row_root[row] = root;
        //     }
        // } else {
            for (int r : removed) {
                auto it = cached_row_root.find(r);
                if (it != cached_row_root.end()) {
                    auto *root = it->second;
                    auto blk_it = cached_root_blocks.find(root);
                    if (blk_it != cached_root_blocks.end()) blk_it->second.rows.erase(r);
                    cached_row_root.erase(it);
                }
                cached_block_rows.erase(r);
            }

            for (int r : added) {
                if (!isRowActive(r)) continue;
                auto it = vertex_repr.find(r);
                if (it == vertex_repr.end()) continue;
                auto *root = treap_get_root(it->second);

                Block &blk = cached_root_blocks[root];
                blk.rows.insert(r);
                if (get_row_cols) {
                    const auto &cols = get_row_cols(r);
                    blk.cols.insert(cols.begin(), cols.end());
                }
                cached_row_root[r] = root;
                cached_block_rows.insert(r);
            }
        // }

        vector<Block> result;
        result.reserve(cached_root_blocks.size());
        for (auto &[r, blk] : cached_root_blocks) {
            if (!blk.rows.empty()) result.push_back(blk);
        }
        return result;
};
