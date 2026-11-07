#include "Bazzalt/AssetManager.h"
#include "Bazzalt/Timeline.h"
#include "Utils/NumberParsing.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <ryml.hpp>
#include <set>
#include <sstream>
namespace Bazzalt {
namespace {
using Node = ryml::ConstNodeRef;
std::string Text(Node n) {
    if (!n.has_val())
        throw std::invalid_argument("Expected timeline scalar");
    auto s = n.val();
    return {s.str, s.len};
}
std::string String(Node n, const char *key, const std::string &fallback = "") {
    return n.has_child(key) ? Text(n[key]) : fallback;
}
double Number(Node n) {
    double value;
    if (!Detail::ParseNumber(Text(n), value))
        throw std::invalid_argument("Invalid timeline number");
    return value;
}
double Number(Node n, const char *key, double fallback) {
    return n.has_child(key) ? Number(n[key]) : fallback;
}
bool Boolean(Node n, const char *key, bool fallback = false) {
    auto s = String(n, key, fallback ? "true" : "false");
    if (s != "true" && s != "false")
        throw std::invalid_argument("Invalid timeline boolean");
    return s == "true";
}
UUID Id(Node n, const char *key) {
    UUID out;
    if (!n.has_child(key) || !UUID::TryParse(Text(n[key]), out))
        throw std::invalid_argument("Invalid timeline UUID");
    return out;
}
std::vector<double> Array(Node n, unsigned count) {
    if (!n.is_seq() || n.num_children() != count)
        throw std::invalid_argument("Invalid timeline vector");
    std::vector<double> out;
    for (auto v : n.children())
        out.push_back(Number(v));
    return out;
}
Vec3 Vector(Node n) {
    auto v = Array(n, 3);
    return {float(v[0]), float(v[1]), float(v[2])};
}
Quaternion Rotation(Node n) {
    auto v = Array(n, 4);
    return {float(v[0]), float(v[1]), float(v[2]), float(v[3])};
}
void CheckName(const std::string &s) {
    if (s.size() > 256 || std::any_of(s.begin(), s.end(), [](unsigned char c) { return c < 32; }))
        throw std::invalid_argument("Timeline name is too long or contains control characters");
}
bool Finite(Vec3 v) {
    return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
}
void Pose(const Transform &t) {
    auto q = t.Rotation;
    double length = double(q.X) * q.X + double(q.Y) * q.Y + double(q.Z) * q.Z + double(q.W) * q.W;
    if (!Finite(t.Position) || !Finite(t.Scale) || !std::isfinite(length) || length < 1e-10 ||
        std::abs(t.Scale.X) < 1e-6 || std::abs(t.Scale.Y) < 1e-6 || std::abs(t.Scale.Z) < 1e-6)
        throw std::invalid_argument("Invalid timeline transform");
}
void Sequence(Node n) {
    if (!n.is_seq() || n.num_children() > 65536)
        throw std::invalid_argument("Invalid timeline list");
}
void Write(std::ostream &out, Vec3 v) {
    out << '[' << v.X << ',' << v.Y << ',' << v.Z << ']';
}
void Write(std::ostream &out, Quaternion v) {
    out << '[' << v.X << ',' << v.Y << ',' << v.Z << ',' << v.W << ']';
}
EventValue ReadArgument(Node n) {
    auto type = String(n, "type");
    if (!n.has_child("value"))
        throw std::invalid_argument("Missing signal argument");
    auto v = n["value"];
    if (type == "bool") {
        auto value = Text(v);
        if (value != "true" && value != "false")
            throw std::invalid_argument("Invalid signal boolean");
        return value == "true";
    }
    if (type == "int") {
        std::int64_t value;
        if (!Detail::ParseNumber(Text(v), value))
            throw std::invalid_argument("Invalid signal integer");
        return value;
    }
    if (type == "number")
        return Number(v);
    if (type == "string")
        return Text(v);
    if (type == "vec2") {
        auto a = Array(v, 2);
        return Vec2{float(a[0]), float(a[1])};
    }
    if (type == "vec3")
        return Vector(v);
    if (type == "vec4") {
        auto a = Array(v, 4);
        return Vec4{float(a[0]), float(a[1]), float(a[2]), float(a[3])};
    }
    if (type == "quaternion")
        return Rotation(v);
    if (type == "uuid") {
        UUID id;
        if (!UUID::TryParse(Text(v), id))
            throw std::invalid_argument("Invalid signal UUID");
        return id;
    }
    throw std::invalid_argument("Unknown signal argument type");
}
void WriteArgument(std::ostream &out, const EventValue &value) {
    static const char *names[] = {"bool", "int",  "number",     "string", "vec2",
                                  "vec3", "vec4", "quaternion", "uuid"};
    out << "{\"type\":" << std::quoted(names[value.index()]) << ",\"value\":";
    std::visit(
        [&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::string>)
                out << std::quoted(v);
            else if constexpr (std::is_same_v<T, UUID>)
                out << std::quoted(v.ToString());
            else if constexpr (std::is_same_v<T, bool>)
                out << (v ? "true" : "false");
            else if constexpr (std::is_same_v<T, Vec2>)
                out << '[' << v.X << ',' << v.Y << ']';
            else if constexpr (std::is_same_v<T, Vec3> || std::is_same_v<T, Quaternion>)
                Write(out, v);
            else if constexpr (std::is_same_v<T, Vec4>)
                out << '[' << v.X << ',' << v.Y << ',' << v.Z << ',' << v.W << ']';
            else
                out << v;
        },
        value);
    out << '}';
}
} // namespace
void TimelineSequence::Validate() const {
    CheckName(this->Name);
    if (!std::isfinite(Duration) || Duration <= 0 || Duration > 86400 || FrameRate < 1 ||
        FrameRate > 240 || Tracks.size() > 1024 || Cameras.size() > 4096 || Signals.size() > 65536)
        throw std::invalid_argument("Timeline exceeds duration or track limits");
    const auto time = [&](double t) {
        if (!std::isfinite(t) || t < 0 || t > Duration)
            throw std::invalid_argument("Timeline time is outside its range");
    };
    std::set<UUID> targets;
    std::size_t count = 0;
    for (auto &track : Tracks) {
        CheckName(track.Name);
        if (!track.Target || !targets.insert(track.Target).second || track.Keys.empty())
            throw std::invalid_argument(
                "Timeline needs unique transform targets and nonempty keys");
        double previous = -1;
        for (auto &key : track.Keys) {
            time(key.Time);
            if (key.Time <= previous || unsigned(key.Interpolation) > 2 || ++count > 100000)
                throw std::invalid_argument("Timeline keys must be unique and sorted");
            previous = key.Time;
            Pose(key.Value);
        }
    }
    double end = 0;
    for (auto &camera : Cameras) {
        CheckName(camera.Name);
        time(camera.Start);
        time(camera.End);
        if (!camera.Camera || camera.End <= camera.Start || camera.Start < end ||
            !std::isfinite(camera.BlendIn) || camera.BlendIn < 0 ||
            camera.BlendIn > camera.End - camera.Start)
            throw std::invalid_argument("Camera shots must be ordered and nonoverlapping");
        end = camera.End;
    }
    double previous = -1;
    for (auto &signal : Signals) {
        CheckName(signal.Name);
        time(signal.Time);
        if (signal.Time < previous || signal.Arguments.size() > 64)
            throw std::invalid_argument("Timeline signals must be ordered");
        previous = signal.Time;
        for (auto &value : signal.Arguments)
            std::visit(
                [](const auto &v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, std::string>)
                        CheckName(v);
                    else if constexpr (std::is_same_v<T, double>) {
                        if (!std::isfinite(v))
                            throw std::invalid_argument("Nonfinite signal argument");
                    } else if constexpr (std::is_same_v<T, Vec3>) {
                        if (!Finite(v))
                            throw std::invalid_argument("Nonfinite signal vector");
                    } else if constexpr (std::is_same_v<T, Vec2> || std::is_same_v<T, Vec4> ||
                                         std::is_same_v<T, Quaternion>) {
                        if (!std::isfinite(v.X) || !std::isfinite(v.Y))
                            throw std::invalid_argument("Nonfinite signal vector");
                        if constexpr (!std::is_same_v<T, Vec2>) {
                            if (!std::isfinite(v.Z) || !std::isfinite(v.W))
                                throw std::invalid_argument("Nonfinite signal vector");
                        }
                    }
                },
                value);
    }
}
TimelineSequence TimelineSequence::FromSource(const std::string &source) {
    if (source.size() > 16 * 1024 * 1024)
        throw std::invalid_argument("Timeline exceeds 16 MiB");
    auto tree = ryml::parse_in_arena({source.data(), source.size()});
    auto root = tree.crootref();
    if (Number(root, "version", 0) != 1)
        throw std::invalid_argument("Unsupported timeline version");
    TimelineSequence out;
    out.Name = String(root, "name", "Timeline");
    out.Duration = Number(root, "duration", 10);
    double fps = Number(root, "frameRate", 30);
    if (fps < 1 || fps > 240 || std::floor(fps) != fps)
        throw std::invalid_argument("Invalid timeline frame rate");
    out.FrameRate = unsigned(fps);
    if (root.has_child("tracks")) {
        auto list = root["tracks"];
        Sequence(list);
        for (auto n : list.children()) {
            TimelineTransformTrack track;
            track.Name = String(n, "name");
            track.Target = Id(n, "target");
            track.WorldSpace = Boolean(n, "worldSpace");
            track.Muted = Boolean(n, "muted");
            auto keys = n["keys"];
            Sequence(keys);
            for (auto row : keys.children()) {
                TimelineTransformKey key;
                key.Time = Number(row, "time", 0);
                key.Value.Position = Vector(row["position"]);
                key.Value.Rotation = Rotation(row["rotation"]);
                key.Value.Scale = Vector(row["scale"]);
                auto interpolation = Number(row, "interpolation", 1);
                if (interpolation < 0 || interpolation > 2 ||
                    interpolation != std::floor(interpolation))
                    throw std::invalid_argument("Invalid interpolation");
                key.Interpolation = TimelineInterpolation(unsigned(interpolation));
                track.Keys.push_back(key);
            }
            out.Tracks.push_back(std::move(track));
        }
    }
    if (root.has_child("cameras")) {
        auto list = root["cameras"];
        Sequence(list);
        for (auto n : list.children())
            out.Cameras.push_back({String(n, "name"), Id(n, "camera"), Number(n, "start", 0),
                                   Number(n, "end", 1), Number(n, "blendIn", 0)});
    }
    if (root.has_child("signals")) {
        auto list = root["signals"];
        Sequence(list);
        for (auto n : list.children()) {
            TimelineSignal s;
            s.Name = String(n, "name");
            s.Time = Number(n, "time", 0);
            s.Event = Id(n, "event");
            if (n.has_child("arguments")) {
                auto args = n["arguments"];
                Sequence(args);
                for (auto arg : args.children())
                    s.Arguments.push_back(ReadArgument(arg));
            }
            out.Signals.push_back(std::move(s));
        }
    }
    out.Validate();
    return out;
}
TimelineSequence TimelineSequence::Load(UUID id) {
    auto asset = AssetManager::GetAsset(id);
    if (!asset || asset->State != AssetState::Ready ||
        asset->SourcePath.extension() != ".btimeline")
        throw std::invalid_argument("Timeline asset is not ready");
    auto size = std::filesystem::file_size(asset->CachePath);
    if (size > 16 * 1024 * 1024)
        throw std::invalid_argument("Timeline exceeds 16 MiB");
    std::ifstream file(asset->CachePath, std::ios::binary);
    std::string source(std::size_t(size), '\0');
    if (!file.read(source.data(), std::streamsize(size)))
        throw std::runtime_error("Cannot read timeline");
    return FromSource(source);
}
std::string TimelineSequence::ToSource() const {
    Validate();
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << "{\"version\":1,\"name\":" << std::quoted(Name)
        << ",\"duration\":" << Duration << ",\"frameRate\":" << FrameRate << ",\"tracks\":[";
    bool first = true;
    for (auto &track : Tracks) {
        if (!first)
            out << ',';
        first = false;
        out << "{\"name\":" << std::quoted(track.Name)
            << ",\"target\":" << std::quoted(track.Target.ToString())
            << ",\"worldSpace\":" << (track.WorldSpace ? "true" : "false")
            << ",\"muted\":" << (track.Muted ? "true" : "false") << ",\"keys\":[";
        bool kfirst = true;
        for (auto &key : track.Keys) {
            if (!kfirst)
                out << ',';
            kfirst = false;
            out << "{\"time\":" << key.Time << ",\"position\":";
            Write(out, key.Value.Position);
            out << ",\"rotation\":";
            Write(out, key.Value.Rotation);
            out << ",\"scale\":";
            Write(out, key.Value.Scale);
            out << ",\"interpolation\":" << unsigned(key.Interpolation) << '}';
        }
        out << "]}";
    }
    out << "],\"cameras\":[";
    first = true;
    for (auto &shot : Cameras) {
        if (!first)
            out << ',';
        first = false;
        out << "{\"name\":" << std::quoted(shot.Name)
            << ",\"camera\":" << std::quoted(shot.Camera.ToString()) << ",\"start\":" << shot.Start
            << ",\"end\":" << shot.End << ",\"blendIn\":" << shot.BlendIn << '}';
    }
    out << "],\"signals\":[";
    first = true;
    for (auto &signal : Signals) {
        if (!first)
            out << ',';
        first = false;
        out << "{\"time\":" << signal.Time << ",\"name\":" << std::quoted(signal.Name)
            << ",\"event\":" << std::quoted(signal.Event.ToString()) << ",\"arguments\":[";
        bool afirst = true;
        for (auto &argument : signal.Arguments) {
            if (!afirst)
                out << ',';
            afirst = false;
            WriteArgument(out, argument);
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}
} // namespace Bazzalt
