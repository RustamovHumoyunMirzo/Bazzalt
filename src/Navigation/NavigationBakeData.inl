// Versioned little-endian layer data. Detour pointers and native struct padding never enter assets.
struct Writer {
    std::vector<std::uint8_t> Bytes;
    void U(unsigned v) {
        for (int i = 0; i < 4; ++i)
            Bytes.push_back(std::uint8_t(v >> (i * 8)));
    }
    void F(float v) {
        U(std::bit_cast<std::uint32_t>(v));
    }
};
struct Reader {
    const std::vector<std::uint8_t> &Bytes;
    std::size_t Offset = 0;
    unsigned U() {
        if (Bytes.size() - Offset < 4)
            throw std::runtime_error("Truncated navigation bake");
        unsigned v = 0;
        for (int i = 0; i < 4; ++i)
            v |= unsigned(Bytes[Offset++]) << (i * 8);
        return v;
    }
    float F() {
        float v = std::bit_cast<float>(U());
        if (!std::isfinite(v))
            throw std::runtime_error("Nonfinite navigation bake value");
        return v;
    }
};
std::vector<std::uint8_t> Capture(Entity e) {
    auto &s = Get(e);
    auto p = s.Cache->getParams();
    Writer w;
    w.U(0x56414e42);
    w.U(1);
    for (float v : p->orig)
        w.F(v);
    w.F(p->cs);
    w.F(p->ch);
    w.U(unsigned(p->width));
    w.F(p->walkableHeight);
    w.F(p->walkableRadius);
    w.F(p->walkableClimb);
    w.F(p->maxSimplificationError);
    unsigned count = 0;
    for (int i = 0; i < s.Cache->getTileCount(); ++i)
        if (s.Cache->getTile(i)->header)
            ++count;
    w.U(count);
    for (int i = 0; i < s.Cache->getTileCount(); ++i) {
        auto tile = s.Cache->getTile(i);
        if (!tile->header)
            continue;
        auto &h = *tile->header;
        w.U(unsigned(h.tx));
        w.U(unsigned(h.ty));
        w.U(unsigned(h.tlayer));
        for (float v : h.bmin)
            w.F(v);
        for (float v : h.bmax)
            w.F(v);
        for (unsigned v :
             {unsigned(h.hmin), unsigned(h.hmax), unsigned(h.width), unsigned(h.height),
              unsigned(h.minx), unsigned(h.maxx), unsigned(h.miny), unsigned(h.maxy)})
            w.U(v);
        auto offset = (sizeof(dtTileCacheLayerHeader) + 3) & ~std::size_t(3);
        w.Bytes.insert(w.Bytes.end(), tile->data + offset, tile->data + tile->dataSize);
        if (w.Bytes.size() > 128 * 1024 * 1024)
            throw std::length_error("Navigation bake exceeds 128 MiB");
    }
    return w.Bytes;
}
void Restore(Entity e, const std::vector<std::uint8_t> &bytes) {
    if (!e || !e.HasComponent<NavMeshSurface>() || !e.GetComponent<NavMeshSurface>().IsValid() ||
        bytes.size() > 128 * 1024 * 1024)
        throw std::invalid_argument("Invalid navigation bake or surface");
    Reader r{bytes};
    if (r.U() != 0x56414e42 || r.U() != 1)
        throw std::runtime_error("Unsupported navigation bake version");
    auto settings = e.GetComponent<NavMeshSurface>();
    dtTileCacheParams p{};
    for (float &v : p.orig)
        v = r.F();
    p.cs = r.F();
    p.ch = r.F();
    auto width = r.U();
    p.width = p.height = int(width);
    p.walkableHeight = r.F();
    p.walkableRadius = r.F();
    p.walkableClimb = r.F();
    p.maxSimplificationError = r.F();
    p.maxTiles = 4096;
    p.maxObstacles = int(settings.MaxObstacles);
    auto count = r.U();
    if (width < 16 || width > 128 || p.cs < .05f || p.cs > 10 || p.ch < .02f || p.ch > 5 ||
        p.walkableHeight <= 0 || p.walkableHeight > 50 || p.walkableRadius <= 0 ||
        p.walkableRadius > 20 || p.walkableClimb < 0 || p.walkableClimb >= p.walkableHeight ||
        p.maxSimplificationError <= 0 || p.maxSimplificationError > 10 || count == 0 ||
        count > 4096 || settings.AgentHeight > p.walkableHeight ||
        settings.AgentRadius > p.walkableRadius)
        throw std::runtime_error(
            "Invalid navigation bake parameters or insufficient baked agent clearance");
    auto result = std::make_shared<Surface>();
    result->Init(e, p);
    result->Settings.AgentRadius = p.walkableRadius;
    result->Settings.AgentHeight = p.walkableHeight;
    std::set<std::array<unsigned, 3>> coordinates;
    for (unsigned i = 0; i < count; ++i) {
        dtTileCacheLayerHeader h{};
        h.magic = DT_TILECACHE_MAGIC;
        h.version = DT_TILECACHE_VERSION;
        auto x = r.U(), y = r.U(), layer = r.U();
        if (x > 1024 || y > 1024 || layer > 255 || !coordinates.insert({x, y, layer}).second)
            throw std::runtime_error("Invalid navigation tile coordinates");
        h.tx = int(x);
        h.ty = int(y);
        h.tlayer = int(layer);
        for (float &v : h.bmin)
            v = r.F();
        for (float &v : h.bmax)
            v = r.F();
        auto low = r.U(), high = r.U(), w = r.U(), height = r.U(), minx = r.U(), maxx = r.U(),
             miny = r.U(), maxy = r.U();
        if (high > 65535 || low > high || w != width || height != width || minx > maxx ||
            maxx >= w || miny > maxy || maxy >= height)
            throw std::runtime_error("Invalid navigation tile bounds");
        for (int axis = 0; axis < 3; ++axis)
            if (h.bmin[axis] > h.bmax[axis] || std::abs(h.bmin[axis]) > 10000000 ||
                std::abs(h.bmax[axis]) > 10000000)
                throw std::runtime_error("Invalid navigation world bounds");
        h.hmin = static_cast<unsigned short>(low);
        h.hmax = static_cast<unsigned short>(high);
        h.width = static_cast<unsigned char>(w);
        h.height = static_cast<unsigned char>(height);
        h.minx = static_cast<unsigned char>(minx);
        h.maxx = static_cast<unsigned char>(maxx);
        h.miny = static_cast<unsigned char>(miny);
        h.maxy = static_cast<unsigned char>(maxy);
        std::size_t cells = w * height, size = cells * 3;
        if (bytes.size() - r.Offset < size)
            throw std::runtime_error("Truncated navigation tile");
        auto data = bytes.data() + r.Offset;
        for (std::size_t k = 0; k < cells; ++k) {
            if (data[cells + k] > 16 && data[cells + k] != 63)
                throw std::runtime_error("Invalid navigation area");
            unsigned x = unsigned(k % w), y = unsigned(k / w), cons = data[cells * 2 + k];
            if (((cons & 1) && x == 0) || ((cons & 2) && y + 1 == height) ||
                ((cons & 4) && x + 1 == w) || ((cons & 8) && y == 0))
                throw std::runtime_error("Navigation layer connection leaves the grid");
        }
        unsigned char *output = nullptr;
        int outputSize = 0;
        Check(dtBuildTileCacheLayer(&result->Compression, &h, data, data + cells, data + cells * 2,
                                    &output, &outputSize));
        result->AddLayer(output, outputSize);
        r.Offset += size;
    }
    if (r.Offset != bytes.size())
        throw std::runtime_error("Trailing navigation bake data");
    Commit(e, std::move(result));
}
