#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <ranges>
#include <iterator>

using namespace std;

int main(int argc, char** argv) {
    auto args = views::counted(argv, argc) | views::drop(1);
    
    char delimiter = '\t';
    vector<int> target_vec;
    string filename = "";

    auto it = args.begin();
    while (it != args.end()) {
        string_view opt(*it);
        if (opt.starts_with("-f")) {
            auto fields_str = string(opt.substr(2));
            ranges::replace(fields_str, ',', ' ');
            stringstream ss(fields_str);
            target_vec.insert(target_vec.end(), istream_iterator<int>(ss), istream_iterator<int>());
        } else if (opt.starts_with("-d")) {
            if (opt.size() > 2) {
                delimiter = opt[2];
            } else if (next(it) != args.end()) {
                delimiter = (*++it)[0];
            }
        } else {
            filename = string(opt);
        }
        if (it != args.end()) ++it;
    }

    if (target_vec.empty()) return 1;

    ranges::sort(target_vec);
    auto [ret_uniq, _] = ranges::unique(target_vec);
    target_vec.erase(ret_uniq, target_vec.end());

    int max_f = target_vec.back();
    vector<uint8_t> h_mask(max_f + 1, 0);
    ranges::for_each(target_vec, [&](int f) { h_mask[f] = 1; });
    uint8_t* d_mask = h_mask.data();

    vector<char> buffer;
    if (filename == "" || filename == "-") {
        buffer.assign(istreambuf_iterator<char>(cin), istreambuf_iterator<char>());
    } else {
        ifstream in(filename);
        if (!in) return 1;
        buffer.assign(istreambuf_iterator<char>(in), istreambuf_iterator<char>());
    }

    if (buffer.empty()) return 0;
    int data_size = buffer.size();
    char* d_ptr = buffer.data();

    auto line_offsets = views::iota(0, data_size) 
                      | views::filter([&](int i) { return d_ptr[i] == '\n'; })
                      | views::transform([](int i) { return i + 1; });

    vector<int> l_starts = {0};
    ranges::copy(line_offsets, back_inserter(l_starts));
    if (!l_starts.empty() && l_starts.back() >= data_size) l_starts.pop_back();

    int n_lines = l_starts.size();
    int fields_to_extract = target_vec.size();
    vector<int> res_starts(n_lines * fields_to_extract);
    vector<int> res_lens(n_lines * fields_to_extract, -1);

    int* d_l_starts = l_starts.data();
    int* d_res_starts = res_starts.data();
    int* d_res_lens = res_lens.data();

    #pragma acc parallel loop copyin(d_ptr[0:data_size], d_l_starts[0:n_lines], d_mask[0:max_f+1]) \
                         copyout(d_res_starts[0:n_lines*fields_to_extract], d_res_lens[0:n_lines*fields_to_extract])
    for (int i = 0; i < n_lines; i++) {
        int pos = d_l_starts[i];
        int current_field = 1;
        int saved_count = 0;

        while (pos < data_size && d_ptr[pos] != '\n' && d_ptr[pos] != '\r') {
            int field_start = pos;
            int field_len = 0;
            
            while (pos < data_size && d_ptr[pos] != delimiter && d_ptr[pos] != '\n' && d_ptr[pos] != '\r') {
                field_len++;
                pos++;
            }

            if (current_field <= max_f && d_mask[current_field]) {
                if (saved_count < fields_to_extract) {
                    d_res_starts[i * fields_to_extract + saved_count] = field_start;
                    d_res_lens[i * fields_to_extract + saved_count] = field_len;
                    saved_count++;
                }
            }

            if (pos < data_size && d_ptr[pos] == delimiter) {
                pos++;
                current_field++;
            } else break;
        }
    }

    auto line_indices = views::iota(0, n_lines);
    ranges::for_each(line_indices, [&](int i) {
        bool first = true;
        bool has_any = false;
        for (int k = 0; k < fields_to_extract; k++) {
            int idx = i * fields_to_extract + k;
            if (res_lens[idx] != -1) {
                if (!first) cout << delimiter;
                if (res_lens[idx] > 0) cout.write(&d_ptr[res_starts[idx]], res_lens[idx]);
                first = false;
                has_any = true;
            }
        }
        if (has_any) cout << '\n';
    });

    return 0;
}