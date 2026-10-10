// mark-position-invariant: add the SPIR-V Invariant decoration to a vertex shader's Position output.
//
// Usage: mark-position-invariant <in.spv> <out.spv>
//
// slangc 2026.17 (Vulkan SDK 1.4.363.0) has no spelling for it: `invariant` does not parse, `[vk::invariant]`
// is an unknown attribute, and `precise` decorates only scalar and vector arithmetic, never the matrix ops
// the position is built from. So the decoration is added to the compiled module, after slangc and before
// spirv-opt. <in.spv> and <out.spv> may be the same file.
//
// Exit 0: written. 1: the module has no OpDecorate BuiltIn Position, or Position is already Invariant.
// 2: bad arguments, an unreadable or unwritable file, or not a little-endian SPIR-V module.

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr uint32_t kSpirvMagic = 0x07230203;
constexpr size_t kHeaderWords = 5;
constexpr uint32_t kOpDecorate = 71;
constexpr uint32_t kDecorationBuiltIn = 11;
constexpr uint32_t kBuiltInPosition = 0;
constexpr uint32_t kDecorationInvariant = 18;

bool readWords(const char *path, std::vector<uint32_t> &words) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    std::vector<unsigned char> raw;
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) raw.insert(raw.end(), buf, buf + n);
    bool ok = !ferror(f);
    fclose(f);
    if (!ok) return false;
    words.clear();
    for (size_t i = 0; i + 4 <= raw.size(); i += 4)
        words.push_back(uint32_t(raw[i]) | uint32_t(raw[i + 1]) << 8 | uint32_t(raw[i + 2]) << 16 | uint32_t(raw[i + 3]) << 24);
    return raw.size() % 4 == 0;
}

bool writeWords(const char *path, const std::vector<uint32_t> &words) {
    std::vector<unsigned char> raw;
    for (uint32_t w : words)
        for (int b = 0; b < 4; b++) raw.push_back(static_cast<unsigned char>(w >> (8 * b)));
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(raw.data(), 1, raw.size(), f) == raw.size();
    return fclose(f) == 0 && ok;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: mark-position-invariant <in.spv> <out.spv>\n");
        return 2;
    }
    std::vector<uint32_t> words;
    if (!readWords(argv[1], words) || words.size() < kHeaderWords || words[0] != kSpirvMagic) {
        printf("not a little-endian SPIR-V module\n");
        return 2;
    }

    // The last Position decoration wins; the Invariant goes right behind it.
    bool found = false;
    uint32_t target = 0;
    size_t insertAt = 0;
    for (size_t i = kHeaderWords; i < words.size();) {
        uint32_t count = words[i] >> 16, op = words[i] & 0xFFFF;
        if (count == 0 || i + count > words.size()) {
            printf("malformed SPIR-V module\n");
            return 2;
        }
        if (op == kOpDecorate && count >= 4 && words[i + 2] == kDecorationBuiltIn && words[i + 3] == kBuiltInPosition) {
            found = true;
            target = words[i + 1];
            insertAt = i + count;
        }
        i += count;
    }
    if (!found) {
        printf("no OpDecorate <id> BuiltIn Position in the module\n");
        return 1;
    }
    for (size_t i = kHeaderWords; i < words.size(); i += words[i] >> 16) {
        if ((words[i] & 0xFFFF) == kOpDecorate && (words[i] >> 16) >= 3 && words[i + 1] == target &&
            words[i + 2] == kDecorationInvariant) {
            printf("Position already Invariant\n");
            return 1;
        }
    }

    const uint32_t decoration[3] = {(3u << 16) | kOpDecorate, target, kDecorationInvariant};
    words.insert(words.begin() + insertAt, decoration, decoration + 3);
    if (!writeWords(argv[2], words)) {
        printf("cannot write %s\n", argv[2]);
        return 2;
    }
    printf("%%%u Invariant added\n", target);
    return 0;
}
