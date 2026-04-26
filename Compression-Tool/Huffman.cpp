#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <queue>
#include <memory>
#include <ranges>
#include <algorithm>
#include <iterator>
#include <openacc.h>

using namespace std;

auto read_file(const string& path) {
    ifstream in(path, ios::binary);
    if (!in) throw runtime_error("invalid file");
    return string(istreambuf_iterator<char>(in), {});
}

auto frequency_table_acc(const string& text) {
    size_t freq_arr[256] = {0};
    const char* data = text.data();
    size_t size = text.size();

    #pragma acc parallel loop copy(freq_arr[0:256]) copyin(data[0:size])
    for (size_t i = 0; i < size; ++i) {
        #pragma acc atomic update
        freq_arr[(unsigned char)data[i]]++;
    }

    return views::iota(0, 256)
        | views::filter([&](int i) { return freq_arr[i] > 0; })
        | views::transform([&](int i) { return pair{(char)i, freq_arr[i]}; })
        | ranges::to<unordered_map<char, size_t>>();
}

struct Node {
    char c;
    size_t f;
    shared_ptr<Node> l, r;
};

auto build_tree(const unordered_map<char, size_t>& freq) {
    auto cmp = [](const auto& a, const auto& b) { return a->f > b->f; };
    priority_queue<shared_ptr<Node>, vector<shared_ptr<Node>>, decltype(cmp)> pq(cmp);

    ranges::for_each(freq, [&](const auto& p) {
        pq.push(make_shared<Node>(p.first, p.second));
    });

    while (pq.size() > 1) {
        auto a = pq.top(); pq.pop();
        auto b = pq.top(); pq.pop();
        pq.push(make_shared<Node>('\0', a->f + b->f, a, b));
    }
    return pq.empty() ? nullptr : pq.top();
}

auto get_codes(shared_ptr<Node> root) {
    unordered_map<char, string> codes;
    auto dfs = [&](auto self, shared_ptr<Node> n, string s) -> void {
        if (!n->l && !n->r) codes[n->c] = s.empty() ? "0" : s;
        else {
            if (n->l) self(self, n->l, s + "0");
            if (n->r) self(self, n->r, s + "1");
        }
    };
    if (root) dfs(dfs, root, "");
    return codes;
}

void write_compressed(ofstream& out, const string& text, const unordered_map<char, string>& codes) {
    auto bits = text 
        | views::transform([&](char c) { return codes.at(c); }) 
        | views::join;

    unsigned char byte = 0;
    int count = 0;
    for (char bit : bits) {
        if (bit == '1') byte |= (1 << (7 - count));
        if (++count == 8) {
            out.put(byte);
            byte = 0;
            count = 0;
        }
    }
    if (count > 0) out.put(byte);
}

void decode_bitstream(ifstream& in, const unordered_map<char, string>& codes, uint64_t total, const string& outpath) {
    auto reverse_map = codes 
        | views::transform([](const auto& p) { return pair{p.second, p.first}; })
        | ranges::to<unordered_map<string, char>>();

    ofstream out(outpath, ios::binary);
    string cur;
    uint64_t written = 0;

    auto process_byte = [&](unsigned char byte) {
        for (int i : views::iota(0, 8) | views::reverse) {
            if (written >= total) return;
            cur += (byte & (1 << i)) ? '1' : '0';
            if (auto it = reverse_map.find(cur); it != reverse_map.end()) {
                out.put(it->second);
                cur.clear();
                written++;
            }
        }
    };

    ranges::for_each(istreambuf_iterator<char>(in), istreambuf_iterator<char>(), 
        [&](char b) { process_byte(static_cast<unsigned char>(b)); });
}

int main(int argc, char** argv) {
    auto args = views::iota(0, argc) 
        | views::transform([&](int i) { return string(argv[i]); }) 
        | ranges::to<vector<string>>();

    if (args.size() != 4) return 1;

    try {
        if (args[1] == "-e") {
            auto text = read_file(args[2]);
            auto codes = get_codes(build_tree(frequency_table_acc(text)));
            ofstream out(args[3], ios::binary);
            
            uint32_t n = codes.size();
            out.write(reinterpret_cast<char*>(&n), sizeof(n));
            ranges::for_each(codes, [&](const auto& p) {
                out.put(p.first);
                uint32_t clen = p.second.size();
                out.write(reinterpret_cast<char*>(&clen), sizeof(clen));
                out.write(p.second.data(), clen);
            });
            
            uint64_t total = text.size();
            out.write(reinterpret_cast<char*>(&total), sizeof(total));
            write_compressed(out, text, codes);

        } else if (args[1] == "-d") {
            ifstream in(args[2], ios::binary);
            uint32_t n; in.read(reinterpret_cast<char*>(&n), sizeof(n));
            
            auto codes = views::iota(0u, n) | views::transform([&](auto) {
                char c; in.get(c);
                uint32_t len; in.read(reinterpret_cast<char*>(&len), sizeof(len));
                string s(len, '\0'); in.read(s.data(), len);
                return pair{c, s};
            }) | ranges::to<unordered_map<char, string>>();

            uint64_t total; in.read(reinterpret_cast<char*>(&total), sizeof(total));
            decode_bitstream(in, codes, total, args[3]);
        }
    } catch (...) { return 1; }
    return 0;
}