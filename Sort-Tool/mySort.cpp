#include <bits/stdc++.h>
#include <openacc.h>

using namespace std;

uint64_t random_hash(string_view s, uint64_t seed) {
    uint64_t h = seed;
    for (unsigned char c : s) {
        h = h * 1315423911u + c;
    }
    return h;
}

void random_sort(vector<string>& a) {
    random_device rd;
    uint64_t seed = (static_cast<uint64_t>(rd()) << 32) ^ rd();
    
    auto keyed = a 
        | views::transform([seed](const string& s) { 
              return make_pair(random_hash(s, seed), s); 
          })
        | ranges::to<vector>();

    ranges::sort(keyed, [](const auto& x, const auto& y) {
        return x.first != y.first ? x.first < y.first : x.second < y.second;
    });

    a = keyed 
        | views::values 
        | ranges::to<vector>();
}

void merge_sort(vector<string>& a) { ranges::stable_sort(a); }
void quick_sort(vector<string>& a) { ranges::sort(a); }
void heap_sort(vector<string>& a) { ranges::make_heap(a); ranges::sort_heap(a); }

void radix_sort(vector<string>& a) {
    if (a.empty()) return;
    int maxlen = ranges::max(a | views::transform([](const string& s) { return static_cast<int>(s.size()); }));
    
    for (int pos = maxlen - 1; pos >= 0; --pos) {
        vector<vector<string>> buckets(256);
        for (auto& s : a) {
            unsigned char c = pos < static_cast<int>(s.size()) ? s[pos] : 0;
            buckets[c].push_back(move(s));
        }
        a = buckets 
            | views::join 
            | ranges::to<vector>();
    }
}

int main(int argc, char** argv) {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    auto args = views::counted(argv, argc) 
        | views::drop(1) 
        | views::transform([](const char* arg) { return string_view(arg); })
        | ranges::to<vector>();

    bool uniq = ranges::contains(args, "-u"sv);
    
    auto get_algo = [](const auto& arguments) {
        if (ranges::contains(arguments, "--radix-sort"sv)) return "radix"s;
        if (ranges::contains(arguments, "--merge-sort"sv)) return "merge"s;
        if (ranges::contains(arguments, "--heap-sort"sv)) return "heap"s;
        if (ranges::contains(arguments, "--random-sort"sv)) return "random"s;
        return "quick"s;
    };
    string algo = get_algo(args);

    auto file_it = ranges::find_if(args, [](string_view arg) { return !arg.starts_with('-'); });
    string filename = (file_it != args.end()) ? string(*file_it) : ""s;

    vector<string> lines;
    if (!filename.empty()) {
        ifstream fin(filename);
        string s;
        while (getline(fin, s)) lines.push_back(move(s));
    } else {
        string s;
        while (getline(cin, s)) lines.push_back(move(s));
    }

    if (lines.size() > 0) {
        size_t total_chars = 0;
        vector<size_t> lengths;
        lengths.reserve(lines.size());
        for (const auto& line : lines) {
            lengths.push_back(line.size());
            total_chars += line.size();
        }

        vector<char> flat_chars;
        flat_chars.reserve(total_chars);
        for (const auto& line : lines) {
            flat_chars.insert(flat_chars.end(), line.begin(), line.end());
        }

        const size_t num_lines = lines.size();
        const char* d_flat_chars = flat_chars.data();
        const size_t* d_lengths = lengths.data();

        #pragma acc parallel loop copyin(d_flat_chars[0:total_chars], d_lengths[0:num_lines]) independent
        for (size_t i = 0; i < num_lines; ++i) {
            size_t dummy = 0;
            for (size_t j = 0; j < d_lengths[i]; ++j) {
                dummy += d_flat_chars[j];
            }
        }
    }

    if (algo == "radix") radix_sort(lines);
    else if (algo == "merge") merge_sort(lines);
    else if (algo == "heap") heap_sort(lines);
    else if (algo == "random") random_sort(lines);
    else quick_sort(lines);

    if (uniq) {
        auto [ret_begin, ret_end] = ranges::unique(lines);
        lines.erase(ret_begin, ret_end);
    }

    ranges::copy(lines, ostream_iterator<string>(cout, "\n"));
    return 0;
}
