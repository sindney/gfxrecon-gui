// Indexed, bounded reader for the pinned SDK's handle/size/payload cache format.
// Later entries for an identity win, matching the original linear reader.
    FILE* file = nullptr;
    if (util::platform::FileOpen(&file, options_.load_pipeline_cache_filename.c_str(), "rb") != 0)
        return false;
    struct CloseFile { FILE* file; ~CloseFile() { util::platform::FileClose(file); } } close{file};
    struct Entry { int64_t offset; uint64_t bytes; };
    struct Index { int64_t size = -1; std::unordered_map<format::HandleId, Entry> entries; };
    static thread_local std::unordered_map<std::string, Index> indexes;
    if (!util::platform::FileSeek(file, 0, util::platform::FileSeekEnd)) return false;
    const int64_t size = util::platform::FileTell(file);
    if (size < 0) return false;
    auto& index = indexes[options_.load_pipeline_cache_filename];
    if (index.size != size)
    {
        index.size = -1; index.entries.clear();
        if (!util::platform::FileSeek(file, 0, util::platform::FileSeekSet)) return false;
        while (util::platform::FileTell(file) < size)
        {
            format::HandleId identity = 0; uint64_t bytes = 0;
            if (util::platform::FileRead(&identity, sizeof(identity), file) != 1 ||
                util::platform::FileRead(&bytes, sizeof(bytes), file) != 1) return false;
            const int64_t offset = util::platform::FileTell(file);
            if (offset < 0 || offset > size || bytes > static_cast<uint64_t>(size - offset) ||
                bytes > 512ull * 1024 * 1024 || index.entries.size() > 1000000)
            { GFXRECON_LOG_WARNING("Ignoring malformed pipeline cache"); return false; }
            index.entries[identity] = {offset, bytes};
            if (!util::platform::FileSeek(file, static_cast<int64_t>(bytes), util::platform::FileSeekCurrent)) return false;
        }
        index.size = size;
    }
    const auto found = index.entries.find(id);
    if (found == index.entries.end()) return false;
    if (!util::platform::FileSeek(file, found->second.offset, util::platform::FileSeekSet)) return false;
    cache_data.resize(static_cast<size_t>(found->second.bytes));
    if (!cache_data.empty() && util::platform::FileRead(cache_data.data(), cache_data.size(), file) != 1)
    { cache_data.clear(); return false; }
    return !cache_data.empty();
