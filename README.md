# Huffman Compressor

A simple, educational implementation of Huffman coding that compresses a text file and can decompress it back to the original.

## How It Works

1. **Read the file** and count the frequency of each character.
2. **Build a Huffman Tree** using a min-heap (greedy algorithm):
   - Always merge the two lowest-frequency nodes.
3. **Walk the tree** to generate a binary code for each character (left = `0`, right = `1`). Frequent characters get shorter codes.
4. **Encode the file**: replace each character with its bit code, pack the bits into bytes, and write to a `.huff` file (along with the frequency table so the tree can be rebuilt for decoding).
5. **Decode**: rebuild the tree from the stored frequency table, then walk it bit-by-bit to recover the original characters.

## Usage

```bash
./huffman compress   input.txt   output.huff
./huffman decompress output.huff decoded.txt
```
