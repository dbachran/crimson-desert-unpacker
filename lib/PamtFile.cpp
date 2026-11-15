#include "PamtFile.h"
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <unordered_map>
#include <filesystem>

namespace fs = std::filesystem;

namespace paz {

struct PathNode {
    uint32_t parentOffset; // 0xFFFFFFFF = root
    std::string name;
};

static uint32_t readVarint(const std::vector<uint8_t> &data, size_t &off) {
    uint32_t val = 0;
    uint32_t shift = 0;
    while (off < data.size()) {
        uint8_t b = data[off++];
        val |= static_cast<uint32_t>(b & 0x7F) << shift;
        if (!(b & 0x80))
            break;
        shift += 7;
    }
    return val;
}

static std::unordered_map<uint32_t, PathNode> parseStringTree(
    const std::vector<uint8_t> &data,
    size_t start,
    size_t size,
    bool legacy)
{
    std::unordered_map<uint32_t, PathNode> nodes;
    size_t off = start;
    size_t end = start + size;
    while (off < end && off < data.size()) {
        uint32_t rel = static_cast<uint32_t>(off - start);
        uint32_t parent;
        if (legacy) {
            memcpy(&parent, data.data() + off, 4);
            off += 4;
        } else {
            uint32_t delta = readVarint(data, off);
            parent = (delta == 0) ? 0xFFFFFFFF : (rel - delta);
        }
        uint8_t slen = data[off++];
        std::string name(reinterpret_cast<const char *>(data.data() + off), slen);
        off += slen;
        nodes[rel] = {parent, std::move(name)};
    }
    return nodes;
}

static std::string buildPath(
    const std::unordered_map<uint32_t, PathNode> &nodes,
    uint32_t nodeRef)
{
    if (nodeRef == 0xFFFFFFFF)
        return "";
    std::string parts[64];
    int depth = 0;
    uint32_t cur = nodeRef;

    while (cur != 0xFFFFFFFF && depth < 64) {
        auto it = nodes.find(cur);
        if (it == nodes.end()) break;
        parts[depth++] = it->second.name;
        cur = it->second.parentOffset;
    }

    std::string result;
    for (int i = depth - 1; i >= 0; --i)
        result += parts[i];
    return result;
}

std::vector<FileEntry> parsePamtFile(const std::string &pamtPath,
                                      const std::string &pazDir) {
    std::ifstream file(pamtPath, std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("Cannot open PAMT file: " + pamtPath);

    file.seekg(0, std::ios::end);
    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0);
    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char *>(data.data()), fileSize);

    if (fileSize < 16)
        throw std::runtime_error("PAMT file too small: " + pamtPath);

    // Header: [magic:4][pazCount:4][hash:4][zero:4]
    size_t off = 0;
    off += 4;

    uint32_t pazCount;
    memcpy(&pazCount, data.data() + off, 4);
    off += 4;

    off += 4; // hash
    off += 4; // zero

    fs::path pamtFsPath(pamtPath);
    std::string stem = pamtFsPath.stem().string();

    std::vector<std::string> pazPaths(pazCount);
    for (uint32_t i = 0; i < pazCount; i++) {
        off += 4; // hash
        off += 4; // size

        int pazNum = std::stoi(stem) + static_cast<int>(i);
        pazPaths[i] = (fs::path(pazDir) / (std::to_string(pazNum) + ".paz")).string();

        if (i < pazCount - 1)
            off += 4;
    }

    // Folder section: [size:4][entries...]
    uint32_t folderSectionSize;
    memcpy(&folderSectionSize, data.data() + off, 4);
    off += 4;

    uint32_t firstWord = 0;
    if (folderSectionSize >= 4 && off + 4 <= fileSize)
        memcpy(&firstWord, data.data() + off, 4);
    bool legacy = (folderSectionSize >= 4 && firstWord == 0xFFFFFFFF);

    auto folderNodes = parseStringTree(data, off, folderSectionSize, legacy);
    off += folderSectionSize;

    // Node section: [size:4][entries...]
    uint32_t nodeSectionSize;
    memcpy(&nodeSectionSize, data.data() + off, 4);
    off += 4;

    auto nodes = parseStringTree(data, off, nodeSectionSize, legacy);
    off += nodeSectionSize;

    // Record section: [folderCount:4][hash:4][folderRecs: folderCount*16][fileRecs...][extTable]
    uint32_t folderCount;
    memcpy(&folderCount, data.data() + off, 4);
    off += 4;
    off += 4; // hash

    struct FolderRec {
        uint32_t start;
        uint32_t count;
        std::string path;
    };
    std::vector<FolderRec> folderRecs;
    folderRecs.reserve(folderCount);
    uint32_t totalFiles = 0;
    for (uint32_t i = 0; i < folderCount; i++) {
        uint32_t fNodeRef, fStart, fCount;
        memcpy(&fNodeRef, data.data() + off, 4);
        memcpy(&fStart, data.data() + off + 4, 4);
        memcpy(&fCount, data.data() + off + 8, 4);
        off += 16;
        folderRecs.push_back({fStart, fCount, buildPath(folderNodes, fNodeRef)});
        totalFiles += fCount;
    }

    size_t fileRecordsStart = off;
    size_t extTableOffset = fileRecordsStart + static_cast<size_t>(totalFiles) * 20;

    std::vector<std::string> exts = {""};
    if (!legacy && extTableOffset + 4 <= fileSize) {
        uint32_t extSize;
        memcpy(&extSize, data.data() + extTableOffset, 4);
        size_t extPos = extTableOffset + 4;
        size_t extEnd = std::min(fileSize, extPos + extSize);
        exts.clear();
        size_t cur = extPos;
        for (size_t p = extPos; p < extEnd; p++) {
            if (data[p] == 0) {
                exts.emplace_back(reinterpret_cast<const char *>(data.data() + cur), p - cur);
                cur = p + 1;
            }
        }
    }

    // File records: 20 bytes each
    // [nodeRef:4][pazOffset:4][compSize:4][origSize:4][flags:4]
    std::vector<FileEntry> entries;
    entries.reserve(totalFiles);

    for (const auto &frec : folderRecs) {
        for (uint32_t i = 0; i < frec.count && off + 20 <= fileSize; i++) {
            uint32_t nodeRef, pazOffset, compSize, origSize, flags;
            memcpy(&nodeRef, data.data() + off, 4);
            memcpy(&pazOffset, data.data() + off + 4, 4);
            memcpy(&compSize, data.data() + off + 8, 4);
            memcpy(&origSize, data.data() + off + 12, 4);
            memcpy(&flags, data.data() + off + 16, 4);

            uint32_t pazIndex = flags & 0xFF;
            uint32_t extIndex = (flags >> 24) & 0xFF;
            const std::string &ext = (extIndex < exts.size()) ? exts[extIndex] : exts[0];

            FileEntry e;
            std::string nodePath = buildPath(nodes, nodeRef) + ext;
            e.fullPath = frec.path.empty() ? nodePath : (frec.path + "/" + nodePath);
            e.pazFilePath = (pazIndex < pazPaths.size()) ? pazPaths[pazIndex] : pazPaths[0];
            e.offset = pazOffset;
            e.compressedSize = compSize;
            e.originalSize = origSize;
            e.flags = flags >> 8;

            entries.push_back(std::move(e));
            off += 20;
        }
    }

    return entries;
}

} // namespace paz
