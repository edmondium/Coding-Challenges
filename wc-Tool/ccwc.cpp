#include <iostream>
#include <fstream>
#include <vector>
#include <cctype>
using namespace std;

int main(int argc, char** argv) {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    bool countLines = false, countWords = false, countChars = false, countBytes = false;
    string filename;

    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        if (a == "-l") countLines = true;
        else if (a == "-w") countWords = true;
        else if (a == "-m") countChars = true;
        else if (a == "-c") countBytes = true;
        else filename = a;
    }
    if (!(countLines || countWords || countChars || countBytes))
        countLines = countWords = countBytes = true;

    istream* in;
    if (filename.empty()) in = &cin;
    else in = new ifstream(filename, ios::binary);

    const size_t BUFSIZE = 1 << 20;
    vector<char> buffer(BUFSIZE);

    size_t lines = 0, words = 0, chars = 0, bytes = 0;
    bool inWordGlobal = false;

    while (in->read(buffer.data(), BUFSIZE) || in->gcount() > 0) {
        size_t n = in->gcount();
        bytes += n;

        #pragma acc parallel loop reduction(+:lines)
        for (size_t i = 0; i < n; i++) {
            if (buffer[i] == '\n') lines++;
        }

        size_t localWords = 0;
        #pragma acc parallel loop reduction(+:localWords)
        for (size_t i = 0; i < n; i++) {
            if (!isspace((unsigned char)buffer[i]) &&
                (i == 0 ? !inWordGlobal : isspace((unsigned char)buffer[i-1]))) {
                localWords++;
            }
        }
        words += localWords;

        inWordGlobal = !isspace((unsigned char)buffer[n-1]);

        #pragma acc parallel loop reduction(+:chars)
        for (size_t i = 0; i < n; i++) {
            unsigned char c = buffer[i];
            if ((c & 0xC0) != 0x80) chars++;
        }
    }

    if (in != &cin) delete in;

    if (countLines) cout << lines << " ";
    if (countWords) cout << words << " ";
    if (countChars) cout << chars << " ";
    if (countBytes) cout << bytes << " ";
    if (!filename.empty()) cout << filename;
    cout << "\n";
}
