#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <queue>
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>

using namespace std;

// ---------------------------------------------------------------------
// 1. Tree node
// ---------------------------------------------------------------------
struct Node {
    char ch;          // character (only meaningful in leaf nodes)
    int freq;         // frequency count (or sum of children's freq)
    int seq;          // tie-break id, assigned in deterministic order (see buildHuffmanTree)
    Node* left;
    Node* right;

    Node(char c, int f, int s, Node* l = nullptr, Node* r = nullptr)
        : ch(c), freq(f), seq(s), left(l), right(r) {}

    bool isLeaf() const { return !left && !right; }
};

// Comparator for the min-heap: smallest frequency has highest priority.
//
// IMPORTANT: when two nodes have equal frequency, priority_queue's order
// between them is unspecified -- it depends on insertion order. Since we
// insert from an unordered_map, insertion order can differ between the
// compress run (map built by scanning the file) and the decompress run
// (map built by reading a serialized table), even though the frequency
// VALUES are identical. That non-determinism would silently build a
// different-shaped tree (hence different codes) during decompression,
// corrupting the output.
//
// Fix: break ties using a stable, content-derived key. We use a
// monotonically increasing "sequence id" assigned in a fixed, sorted
// order (see buildHuffmanTree) so the same multiset of (char, freq)
// pairs always produces the same tree, regardless of map iteration order.
struct Compare {
    bool operator()(Node* a, Node* b) {
        if (a->freq != b->freq) return a->freq > b->freq; // min-heap
        return a->seq > b->seq; // tie-break deterministically
    }
};

// ---------------------------------------------------------------------
// 2. Build the Huffman Tree from a frequency map
//    Classic greedy algorithm using a priority queue (min-heap)
// ---------------------------------------------------------------------
Node* buildHuffmanTree(const unordered_map<char, int>& freqMap) {
    priority_queue<Node*, vector<Node*>, Compare> pq;

    // Copy into a vector and sort by character first. This guarantees
    // leaves are inserted into the heap in the SAME order every time,
    // regardless of how the unordered_map happened to iterate -- which
    // is what makes tie-breaking (and therefore the resulting tree)
    // deterministic between the compress and decompress runs.
    vector<pair<char, int>> sortedFreqs(freqMap.begin(), freqMap.end());
    sort(sortedFreqs.begin(), sortedFreqs.end(),
         [](const pair<char,int>& a, const pair<char,int>& b) {
             return a.first < b.first;
         });

    int nextSeq = 0;
    for (auto& pair : sortedFreqs) {
        pq.push(new Node(pair.first, pair.second, nextSeq++));
    }

    // Edge case: only one unique character in the file
    if (pq.size() == 1) {
        Node* only = pq.top();
        return new Node('\0', only->freq, nextSeq++, only, nullptr);
    }

    // Repeatedly merge the two smallest nodes until one tree remains
    while (pq.size() > 1) {
        Node* left = pq.top(); pq.pop();
        Node* right = pq.top(); pq.pop();

        // Internal node: '\0' as placeholder char, freq = sum of children.
        // seq keeps increasing so newer internal nodes break ties
        // consistently with older ones too.
        Node* merged = new Node('\0', left->freq + right->freq, nextSeq++, left, right);
        pq.push(merged);
    }

    return pq.top(); // root of the Huffman tree
}

// ---------------------------------------------------------------------
// 3. Walk the tree to generate binary codes for each character
//    left edge = '0', right edge = '1'
// ---------------------------------------------------------------------
void generateCodes(Node* root, const string& path,
                    unordered_map<char, string>& codes) {
    if (!root) return;

    if (root->isLeaf()) {
        // Handle the edge case of a single unique character:
        // give it code "0" so encoding isn't empty.
        codes[root->ch] = path.empty() ? "0" : path;
        return;
    }

    generateCodes(root->left, path + "0", codes);
    generateCodes(root->right, path + "1", codes);
}

// ---------------------------------------------------------------------
// 4. Free the tree memory (avoid leaks)
// ---------------------------------------------------------------------
void freeTree(Node* root) {
    if (!root) return;
    freeTree(root->left);
    freeTree(root->right);
    delete root;
}

// ---------------------------------------------------------------------
// 5. Compression
// ---------------------------------------------------------------------
void compress(const string& inputPath, const string& outputPath) {
    // --- Read the whole input file into a string ---
    ifstream inFile(inputPath, ios::binary);
    if (!inFile) {
        cerr << "Error: cannot open input file " << inputPath << "\n";
        return;
    }
    stringstream buffer;
    buffer << inFile.rdbuf();
    string text = buffer.str();
    inFile.close();

    if (text.empty()) {
        cerr << "Error: input file is empty.\n";
        return;
    }

    // --- Step 1: frequency count ---
    unordered_map<char, int> freqMap;
    for (char c : text) freqMap[c]++;

    // --- Step 2: build tree, Step 3: generate codes ---
    Node* root = buildHuffmanTree(freqMap);
    unordered_map<char, string> codes;
    generateCodes(root, "", codes);

    // --- Step 4: build the encoded bit string ---
    string bitString;
    bitString.reserve(text.size() * 2); // rough guess to reduce reallocations
    for (char c : text) bitString += codes[c];

    // --- Step 5: write output file ---
    // File format:
    //   [uint32] number of distinct characters
    //   for each: [char][uint32 frequency]      <- lets us rebuild the tree
    //   [uint32] number of bits in the encoded stream (need this to know
    //            where real data ends, since last byte may be padded)
    //   [packed bytes] the actual compressed bits
    ofstream outFile(outputPath, ios::binary);
    if (!outFile) {
        cerr << "Error: cannot open output file " << outputPath << "\n";
        freeTree(root);
        return;
    }

    uint32_t numChars = freqMap.size();
    outFile.write(reinterpret_cast<char*>(&numChars), sizeof(numChars));
    for (auto& pair : freqMap) {
        outFile.write(&pair.first, sizeof(char));
        uint32_t f = pair.second;
        outFile.write(reinterpret_cast<char*>(&f), sizeof(f));
    }

    uint32_t numBits = bitString.size();
    outFile.write(reinterpret_cast<char*>(&numBits), sizeof(numBits));

    // Pack the bit string into actual bytes, 8 bits at a time
    unsigned char currentByte = 0;
    int bitCount = 0;
    for (char bit : bitString) {
        currentByte = (currentByte << 1) | (bit - '0');
        bitCount++;
        if (bitCount == 8) {
            outFile.write(reinterpret_cast<char*>(&currentByte), 1);
            currentByte = 0;
            bitCount = 0;
        }
    }
    // Flush any leftover bits (pad with 0s on the right)
    if (bitCount > 0) {
        currentByte <<= (8 - bitCount);
        outFile.write(reinterpret_cast<char*>(&currentByte), 1);
    }
    outFile.close();

    // --- Report compression stats (great for your demo!) ---
    size_t originalBits = text.size() * 8;
    size_t compressedBits = numBits;
    double ratio = 100.0 * (1.0 - (double)compressedBits / originalBits);

    cout << "Compression complete.\n";
    cout << "Original size   : " << text.size() << " bytes\n";
    cout << "Compressed size : " << (numBits + 7) / 8 + numChars * 5 + 8
         << " bytes (incl. frequency table header)\n";
    cout << "Space saved     : " << ratio << "%\n";

    freeTree(root);
}

// ---------------------------------------------------------------------
// 6. Decompression
// ---------------------------------------------------------------------
void decompress(const string& inputPath, const string& outputPath) {
    ifstream inFile(inputPath, ios::binary);
    if (!inFile) {
        cerr << "Error: cannot open input file " << inputPath << "\n";
        return;
    }

    // --- Read frequency table ---
    uint32_t numChars;
    inFile.read(reinterpret_cast<char*>(&numChars), sizeof(numChars));

    unordered_map<char, int> freqMap;
    for (uint32_t i = 0; i < numChars; i++) {
        char c;
        uint32_t f;
        inFile.read(&c, sizeof(char));
        inFile.read(reinterpret_cast<char*>(&f), sizeof(f));
        freqMap[c] = f;
    }

    uint32_t numBits;
    inFile.read(reinterpret_cast<char*>(&numBits), sizeof(numBits));

    // --- Rebuild the exact same tree from the frequency table ---
    Node* root = buildHuffmanTree(freqMap);

    // --- Read the rest of the file as raw bytes ---
    vector<unsigned char> bytes((istreambuf_iterator<char>(inFile)),
                                  istreambuf_iterator<char>());
    inFile.close();

    // --- Walk the tree bit by bit to decode ---
    string result;
    result.reserve(numBits / 8 + 1);

    Node* curr = root;
    uint32_t bitsDecoded = 0;

    // Special case: only one unique character existed
    bool singleChar = root->left && !root->right && root->left->isLeaf();

    for (unsigned char byte : bytes) {
        for (int i = 7; i >= 0 && bitsDecoded < numBits; i--, bitsDecoded++) {
            int bit = (byte >> i) & 1;

            if (singleChar) {
                // Every bit maps to the single character
                result += root->left->ch;
                continue;
            }

            curr = (bit == 0) ? curr->left : curr->right;
            if (curr->isLeaf()) {
                result += curr->ch;
                curr = root; // reset to root for next character
            }
        }
    }

    ofstream outFile(outputPath, ios::binary);
    outFile << result;
    outFile.close();

    cout << "Decompression complete. Output written to " << outputPath << "\n";

    freeTree(root);
}

// ---------------------------------------------------------------------
// main: simple CLI
// ---------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc != 4) {
        cout << "Usage:\n";
        cout << "  " << argv[0] << " compress   <input.txt>  <output.huff>\n";
        cout << "  " << argv[0] << " decompress <input.huff> <output.txt>\n";
        return 1;
    }

    string mode = argv[1];
    string inputPath = argv[2];
    string outputPath = argv[3];

    if (mode == "compress") {
        compress(inputPath, outputPath);
    } else if (mode == "decompress") {
        decompress(inputPath, outputPath);
    } else {
        cerr << "Unknown mode: " << mode << " (use 'compress' or 'decompress')\n";
        return 1;
    }

    return 0;
}
