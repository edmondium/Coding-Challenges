#include <bits/stdc++.h>
using namespace std;

auto main(int argc, char** argv) -> int {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    vector<string> args(argv + 1, argv + argc);
    bool countLines = false, countWords = false, countChars = false, countBytes = false;
    string filename;

    for (auto& a : args)
        if (a == "-l") countLines = true;
        else if (a == "-w") countWords = true;
        else if (a == "-m") countChars = true;
        else if (a == "-c") countBytes = true;
        else filename = a;

    if (!(countLines || countWords || countChars || countBytes))
        countLines = countWords = countBytes = true;

    istream* in;
    if (filename.empty()) in = &cin;
    else in = new ifstream(filename, ios::binary);

    string data((istreambuf_iterator<char>(*in)), {});
    if (in != &cin) delete in;

    size_t lines = 0, words = 0, chars = 0, bytes = data.size();

    for (size_t i = 0; i < data.size();) {
        unsigned char c = data[i];
        if (c < 0x80) i += 1;
        else if ((c >> 5) == 0x6) i += 2;
        else if ((c >> 4) == 0xE) i += 3;
        else if ((c >> 3) == 0x1E) i += 4;
        else i += 1;
        chars++;
    }

    bool inWord = false;
    #pragma acc parallel loop reduction(+:lines,words)
    for (size_t i = 0; i < data.size(); i++) {
        unsigned char c = data[i];
        if (c == '\n') lines++;
        if (isspace(c)) {
            if (inWord) inWord = false;
        } else {
            if (!inWord) {
                words++;
                inWord = true;
            }
        }
    }

    if (countLines) cout << lines << " ";
    if (countWords) cout << words << " ";
    if (countChars) cout << chars << " ";
    if (countBytes) cout << bytes << " ";
    if (!filename.empty()) cout << filename;
    cout << "\n";
}
