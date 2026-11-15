using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace PazGui.Models;

public record FileEntry(
    string FullPath,
    string PazFilePath,
    uint Offset,
    uint CompressedSize,
    uint OriginalSize,
    uint Flags
)
{
    /// <summary>Compression type from PAMT flags: 0=none, 2=LZ4, 3=custom, 4=zlib.</summary>
    public int CompressionType => (int)((Flags >> 8) & 0x0F);

    /// <summary>Whether the entry is ChaCha20-encrypted (from PAMT flags).</summary>
    public bool IsEncrypted => ((Flags >> 12) & 0x0F) != 0;

    /// <summary>Whether the entry is compressed (comp_size != orig_size).</summary>
    public bool IsCompressed => CompressedSize != OriginalSize && CompressedSize > 0;
}

public static class PamtParser
{
    public static List<FileEntry> Parse(string pamtPath, string pazDir)
    {
        var data = File.ReadAllBytes(pamtPath);
        if (data.Length < 16)
            throw new InvalidDataException($"PAMT file too small: {pamtPath}");

        int off = 0;
        off += 4; // magic

        // PAZ count
        uint pazCount = BitConverter.ToUInt32(data, off);
        off += 4;
        off += 4; // hash
        off += 4; // zero

        // PAZ table: pazCount entries of [hash:4][size:4] with [sep:4] between
        string stem = Path.GetFileNameWithoutExtension(pamtPath);
        int baseNum = int.Parse(stem);
        var pazPaths = new string[pazCount];
        for (int i = 0; i < (int)pazCount; i++)
        {
            off += 4; // hash
            off += 4; // size
            pazPaths[i] = Path.Combine(pazDir, (baseNum + i) + ".paz");
            if (i < (int)pazCount - 1)
                off += 4; // separator
        }

        // Folder section: [size:4][entries...]
        uint folderSectionSize = BitConverter.ToUInt32(data, off);
        off += 4;

        bool legacy = folderSectionSize >= 4 && off + 4 <= data.Length &&
                      BitConverter.ToUInt32(data, off) == 0xFFFFFFFF;

        var folderNodes = ParseStringTree(data, off, (int)folderSectionSize, legacy);
        off += (int)folderSectionSize;

        // Node section: [size:4][entries...]
        uint nodeSectionSize = BitConverter.ToUInt32(data, off);
        off += 4;

        var nodes = ParseStringTree(data, off, (int)nodeSectionSize, legacy);
        off += (int)nodeSectionSize;

        // Record section: [folderCount:4][hash:4][folderRecs...][fileRecs...][extTable]
        uint folderCount = BitConverter.ToUInt32(data, off);
        off += 4;
        off += 4; // hash

        var folderRecs = new List<(uint Start, uint Count, string Path)>((int)folderCount);
        uint totalFiles = 0;
        for (int i = 0; i < (int)folderCount; i++)
        {
            uint fNodeRef = BitConverter.ToUInt32(data, off);
            uint fStart = BitConverter.ToUInt32(data, off + 4);
            uint fCount = BitConverter.ToUInt32(data, off + 8);
            off += 16;
            folderRecs.Add((fStart, fCount, BuildPath(folderNodes, fNodeRef)));
            totalFiles += fCount;
        }

        int extTableOffset = off + (int)totalFiles * 20;
        var exts = new List<string> { "" };
        if (!legacy && extTableOffset + 4 <= data.Length)
        {
            uint extSize = BitConverter.ToUInt32(data, extTableOffset);
            int extPos = extTableOffset + 4;
            int extEnd = Math.Min(data.Length, extPos + (int)extSize);
            exts.Clear();
            int cur = extPos;
            for (int p = extPos; p < extEnd; p++)
            {
                if (data[p] == 0)
                {
                    exts.Add(Encoding.UTF8.GetString(data, cur, p - cur));
                    cur = p + 1;
                }
            }
        }

        // File records: 20 bytes each
        // [nodeRef:4][pazOffset:4][compSize:4][origSize:4][flags:4]
        // flags & 0xFF = PAZ file index, (flags >> 24) & 0xFF = extension index
        var entries = new List<FileEntry>((int)totalFiles);
        foreach (var frec in folderRecs)
        {
            for (uint i = 0; i < frec.Count && off + 20 <= data.Length; i++)
            {
                uint nodeRef = BitConverter.ToUInt32(data, off);
                uint pazOffset = BitConverter.ToUInt32(data, off + 4);
                uint compSize = BitConverter.ToUInt32(data, off + 8);
                uint origSize = BitConverter.ToUInt32(data, off + 12);
                uint flags = BitConverter.ToUInt32(data, off + 16);

                uint pazIndex = flags & 0xFF;
                uint extIndex = (flags >> 24) & 0xFF;
                string ext = extIndex < exts.Count ? exts[(int)extIndex] : "";
                string pazFile = pazIndex < pazPaths.Length ? pazPaths[pazIndex] : pazPaths[0];

                string nodePath = BuildPath(nodes, nodeRef) + ext;
                string fullPath = string.IsNullOrEmpty(frec.Path)
                    ? nodePath
                    : frec.Path + "/" + nodePath;

                entries.Add(new FileEntry(fullPath, pazFile, pazOffset, compSize, origSize, flags >> 8));
                off += 20;
            }
        }

        return entries;
    }

    private static uint ReadVarint(byte[] data, ref int off)
    {
        uint val = 0;
        int shift = 0;
        while (off < data.Length)
        {
            byte b = data[off++];
            val |= (uint)(b & 0x7F) << shift;
            if ((b & 0x80) == 0)
                break;
            shift += 7;
        }
        return val;
    }

    private static Dictionary<uint, (uint Parent, string Name)> ParseStringTree(
        byte[] data, int start, int size, bool legacy)
    {
        var nodes = new Dictionary<uint, (uint Parent, string Name)>();
        int off = start;
        int end = start + size;
        while (off < end && off < data.Length)
        {
            uint relOff = (uint)(off - start);
            uint parent;
            if (legacy)
            {
                parent = BitConverter.ToUInt32(data, off);
                off += 4;
            }
            else
            {
                uint delta = ReadVarint(data, ref off);
                parent = delta == 0 ? 0xFFFFFFFF : relOff - delta;
            }
            byte slen = data[off++];
            string name = Encoding.UTF8.GetString(data, off, slen);
            off += slen;
            nodes[relOff] = (parent, name);
        }
        return nodes;
    }

    private static string BuildPath(Dictionary<uint, (uint Parent, string Name)> nodes, uint nodeRef)
    {
        if (nodeRef == 0xFFFFFFFF)
            return "";
        var parts = new List<string>();
        uint cur = nodeRef;
        var seen = new HashSet<uint>();

        while (cur != 0xFFFFFFFF && nodes.ContainsKey(cur))
        {
            if (!seen.Add(cur)) break;
            var (parent, name) = nodes[cur];
            parts.Add(name);
            cur = parent;
        }

        parts.Reverse();
        return string.Concat(parts);
    }
}
