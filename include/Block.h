#ifndef BLOCK_H
#define BLOCK_H
#pragma once

// portable replacement for GCC-only <bits/stdc++.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <set>
#include <vector>
#include <string>
#include <unordered_map>
#include <utility>
using namespace std;

struct Block {
    set<int> rows;  // 舞蹈链行id集合 
    set<int> cols;  // 从1开始编号,对应舞蹈链列数id
    bool is_spilited = false;
    uint64_t activeEntries = 0;
    bool activeEntriesValid = false;
    uint64_t decompositionThreshold = 0;
    bool decompositionStopped = false;
    uint64_t bfsStopArea = 0;
    uint64_t activeRowCount = 0;
    bool trackBfsRows = false;
    std::unordered_map<int, uint32_t> rowCoverCounts;
    
    Block() = default;

    size_t size() const { return rows.size(); }

    Block(set<int> r, set<int> c) : rows(move(r)), cols(move(c)) {}

    template <typename RowsContainer, typename ColsContainer>
    Block(const RowsContainer& r, const ColsContainer& c)
        : rows(r.begin(), r.end()), cols(c.begin(), c.end()) {}

    Block(const Block& other) {
        cols = other.cols;
        rows = other.rows;
        is_spilited = other.is_spilited;
        activeEntries = other.activeEntries;
        activeEntriesValid = other.activeEntriesValid;
        decompositionThreshold = other.decompositionThreshold;
        decompositionStopped = other.decompositionStopped;
        bfsStopArea = other.bfsStopArea;
        activeRowCount = other.activeRowCount;
        trackBfsRows = other.trackBfsRows;
        rowCoverCounts = other.rowCoverCounts;
    };

    void printBlock(int block_id) {
        if (rows.empty() || cols.empty()) return;
        cout << "Block " << block_id << ": { ";

        cout<< "cols: [ ";
        for(int c : cols){
            cout << c << " ";
        }
        cout << "], ";
        cout<< "rows: [ ";
        for(int r : rows){
            cout<< r << " ";
        }
        cout << "] }" << endl;
    }
};

#endif // BLOCK_H
