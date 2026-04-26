#include <bits/stdc++.h>
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

    unordered_map<char, size_t> freq;
    for (int i = 0; i < 256; ++i) {
        if (freq_arr[i] > 0) freq[(char)i] = freq_arr[i];
    }
    return freq;
}

struct Node {
    char c;
    size_t f;
    shared_ptr<Node> l, r;
};

auto build_tree(const unordered_map<char, size_t>& freq) {
    auto cmp = [](const auto& a, const auto& b) { return a->f > b->f; };
    priority_queue<shared_ptr<Node>, vector<shared_ptr<Node>>, decltype(cmp)> pq(cmp);
    for (auto& [c, f] : freq) pq.push(make_shared<Node>(c, f));
    if (pq.empty()) return shared_ptr<Node>();
    while (pq.size() > 1) {
        auto a = pq.top(); pq.pop();
        auto b = pq.top(); pq.pop();
        pq.push(make_shared<Node>('\0', a->f + b->f, a, b));
    }
    return pq.top();
}

void build_codes(shared_ptr<Node> n, string s, unordered_map<char, string>& codes) {
    if (!n) return;
    if (!n->l && !n->r) codes[n->c] = s.empty() ? "0" : s;
    else {
        build_codes(n->l, s + "0", codes);
        build_codes(n->r, s + "1", codes);
    }
}

void write_header(ofstream& out, const unordered_map<char, string>& codes, size_t len) {
    uint32_t n = codes.size();
    out.write(reinterpret_cast<const char*>(&n), sizeof(n));
    for (auto& [c, code] : codes) {
        out.put(c);
        uint32_t clen = code.size();
        out.write(reinterpret_cast<const char*>(&clen), sizeof(clen));
        out.write(code.data(), clen);
    }
    uint64_t total = len;
    out.write(reinterpret_cast<const char*>(&total), sizeof(total));
}

void write_compressed(ofstream& out, const string& text, const unordered_map<char, string>& codes) {
    unsigned char byte = 0;
    int count = 0;
    for (char c : text) {
        for (char bit : codes.at(c)) {
            if (bit == '1') byte |= (1 << (7 - count));
            if (++count == 8) {
                out.put(byte);
                byte = 0;
                count = 0;
            }
        }
    }
    if (count > 0) out.put(byte);
}

void decode_bitstream(ifstream& in, const unordered_map<char, string>& codes, uint64_t total, const string& outpath) {
    unordered_map<string, char> reverse;
    for (auto& [c, code] : codes) reverse[code] = c;
    ofstream out(outpath, ios::binary);
    string cur;
    uint64_t written = 0;
    char byte;
    while (written < total && in.get(byte)) {
        for (int i = 7; i >= 0 && written < total; --i) {
            cur += (byte & (1 << i)) ? '1' : '0';
            if (reverse.contains(cur)) {
                out.put(reverse[cur]);
                cur.clear();
                written++;
            }
        }
    }
}

int main(int argc, char** argv) {
    if (argc != 4) return 1;
    string mode = argv[1];
    try {
        if (mode == "-e") {
            auto text = read_file(argv[2]);
            auto freq = frequency_table_acc(text);
            auto root = build_tree(freq);
            unordered_map<char, string> codes;
            build_codes(root, "", codes);
            ofstream out(argv[3], ios::binary);
            write_header(out, codes, text.size());
            write_compressed(out, text, codes);
        } else if (mode == "-d") {
            ifstream in(argv[2], ios::binary);
            uint32_t n;
            in.read(reinterpret_cast<char*>(&n), sizeof(n));
            unordered_map<char, string> codes;
            for (uint32_t i = 0; i < n; ++i) {
                char c; in.get(c);
                uint32_t len; in.read(reinterpret_cast<char*>(&len), sizeof(len));
                string s(len, '\0'); in.read(s.data(), len);
                codes[c] = s;
            }
            uint64_t total;
            in.read(reinterpret_cast<char*>(&total), sizeof(total));
            decode_bitstream(in, codes, total, argv[3]);
        }
    } catch (...) {
        return 1;
    }
    return 0;
}