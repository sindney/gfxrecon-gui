// CLI replay/compare/shader/perf contracts. Desktop replays need local-tools.json and samples/; skipped with a note otherwise.
#include "cli/cli.h"
#include "core/image_io.h"
#include <array>
#include <fstream>
#include <iostream>
#include <sstream>

namespace gfx::tests {
namespace {
namespace fs = std::filesystem;
void Require(bool condition, const std::string& message) { if (!condition) throw std::runtime_error("Replay CLI check failed: " + message); }
struct Outcome { int exit; Json document; std::string text; };
Outcome Invoke(const std::vector<std::string>& args) {
    std::ostringstream out; std::atomic_bool cancel{false};
    const auto exit = cli::Main(args, out, cancel);
    return {exit, Json::parse(out.str()), out.str()};
}
std::vector<std::string> Join(std::vector<std::string> a, const std::vector<std::string>& b) { a.insert(a.end(), b.begin(), b.end()); return a; }
std::string Code(const Outcome& o) { return o.document.contains("error") ? o.document["error"].value("code", "") : ""; }
Image Solid(uint32_t w, uint32_t h, std::array<uint8_t, 4> color) {
    Image image; image.width = w; image.height = h; image.rgba.resize(size_t(w) * h * 4);
    for (size_t p = 0; p < size_t(w) * h; ++p) for (int c = 0; c < 4; ++c) image.rgba[p * 4 + c] = color[c];
    return image;
}
// Bottom-up 24-bit BMP as written by gfxrecon-replay.
void WriteBmp24(const fs::path& file, const Image& image) {
    const uint32_t pitch = (image.width * 3 + 3) & ~3u, size = 54 + pitch * image.height;
    std::vector<uint8_t> bytes(size, 0);
    auto put32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) bytes[at + i] = uint8_t(v >> (8 * i)); };
    bytes[0] = 'B'; bytes[1] = 'M'; put32(2, size); put32(10, 54); put32(14, 40); put32(18, image.width); put32(22, image.height);
    bytes[26] = 1; bytes[28] = 24; put32(34, pitch * image.height);
    for (uint32_t y = 0; y < image.height; ++y) for (uint32_t x = 0; x < image.width; ++x) {
        const auto* src = &image.rgba[(size_t(y) * image.width + x) * 4]; auto* dst = &bytes[54 + size_t(image.height - 1 - y) * pitch + x * 3];
        dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0];
    }
    std::ofstream out(file, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}
void ImageTests(const fs::path& root, const std::vector<std::string>& global) {
    const auto dir = root / "replay-cli-images"; fs::create_directories(dir);
    auto a = Solid(8, 4, {10, 20, 30, 255}), b = a; b.rgba[0] += 5; b.rgba[(3 * 8 + 7) * 4 + 2] += 20;
    WritePng(dir / "a.png", a); WritePng(dir / "a2.png", a); WritePng(dir / "b.png", b); WritePng(dir / "big.png", Solid(9, 4, {10, 20, 30, 255}));
    // PNG round trip and BMP (bottom-up, 24-bit, row padding) -> PNG conversion keep every pixel.
    auto loaded = LoadImage(dir / "b.png"); Require(loaded.width == 8 && loaded.height == 4 && loaded.rgba == b.rgba, "png round trip");
    auto gradient = Solid(5, 3, {0, 0, 0, 255});
    for (size_t p = 0; p < 15; ++p) { gradient.rgba[p * 4] = uint8_t(p * 10); gradient.rgba[p * 4 + 1] = uint8_t(200 - p); gradient.rgba[p * 4 + 2] = uint8_t(p); }
    WriteBmp24(dir / "g.bmp", gradient);
    const auto decoded = ConvertToPng(dir / "g.bmp", dir / "g.png");
    Require(decoded.rgba == gradient.rgba && LoadImage(dir / "g.png").rgba == gradient.rgba, "bmp to png conversion");
    // Identical images: zero differences, infinite PSNR reported as null.
    const auto same = Invoke(Join({"compare", (dir / "a.png").u8string(), (dir / "a2.png").u8string()}, global));
    Require(same.exit == 0 && same.document["ok"] == true, "identical compare ok");
    const auto& s = same.document["result"];
    Require(s["identical"] == true && s["differing_pixels"] == 0 && s["max_abs_diff_overall"] == 0 && s["psnr_db"].is_null() && s["extent_equal"] == true, "identical metrics");
    // Different images: two differing pixels, exact channel maxima, finite PSNR and a diff image.
    const auto diff = Invoke(Join({"compare", (dir / "a.png").u8string(), (dir / "b.png").u8string(), "--out", (dir / "diff.png").u8string()}, global));
    Require(diff.exit == 0, "different compare ok");
    const auto& d = diff.document["result"];
    Require(d["identical"] == false && d["differing_pixels"] == 2 && d["max_abs_diff"]["r"] == 5 && d["max_abs_diff"]["b"] == 20 && d["max_abs_diff"]["g"] == 0, "difference metrics");
    Require(d["psnr_db"].is_number() && d["psnr_db"].get<double>() > 30 && fs::is_regular_file(dir / "diff.png"), "psnr and diff image");
    const auto diffImage = LoadImage(dir / "diff.png"); Require(diffImage.width == 8 && diffImage.height == 4, "diff image extent");
    const auto bmp = Invoke(Join({"compare", (dir / "g.bmp").u8string(), (dir / "g.png").u8string()}, global));
    Require(bmp.exit == 0 && bmp.document["result"]["identical"] == true, "bmp vs png compare");
    // Mismatched extents are an error, never a difference.
    const auto extent = Invoke(Join({"compare", (dir / "a.png").u8string(), (dir / "big.png").u8string()}, global));
    Require(extent.exit == cli::kFailed && Code(extent) == "extent_mismatch" && extent.document["error"]["detail"]["extent_equal"] == false, "extent mismatch is an error");
    Require(Code(Invoke(Join({"compare", (dir / "a.png").u8string(), (dir / "missing.png").u8string()}, global))) == "image_not_found", "missing image");
    { std::ofstream bad(dir / "bad.png", std::ios::binary); bad << "not an image"; }
    Require(Code(Invoke(Join({"compare", (dir / "a.png").u8string(), (dir / "bad.png").u8string()}, global))) == "image_unreadable", "corrupt image");
}
fs::path FindUpwards(const fs::path& start, const fs::path& relative) {
    for (auto dir = fs::absolute(start);; dir = dir.parent_path()) {
        if (fs::exists(dir / relative)) return dir / relative;
        if (dir == dir.parent_path()) return {};
    }
}
bool HasFiles(const fs::path& directory, const std::string& name) {
    if (!fs::exists(directory)) return false;
    for (const auto& entry : fs::recursive_directory_iterator(directory)) if (entry.path().filename() == name) return true;
    return false;
}
} // namespace

void RunReplayCliTests(const fs::path& root) {
    struct Cleanup { fs::path root; ~Cleanup() { std::error_code ignored; for (const auto* name : {"replay-cli-images", "replay-cli-settings", "replay-cli-work"}) fs::remove_all(root / name, ignored); } } cleanup{root};
    using namespace gfx::cli;
    RegisterAll();
    const auto settings = (root / "replay-cli-settings").u8string();
    const std::vector<std::string> global{"--quiet", "--settings-dir", settings, "--target", "local"};
    for (const auto* name : {"replay frame", "replay draw", "compare", "shader show", "shader export", "shader compile", "shader check", "perf"})
        Require(std::any_of(Commands().begin(), Commands().end(), [&](const Command& c) { return c.Name() == name; }), std::string("registered: ") + name);
    ImageTests(root, global);
    // Option contract errors need no tools.
    Require(Invoke(Join({"replay", "frame", "--capture", "x.gfxr"}, global)).exit == kUsage, "frame requires --frame");
    Require(Invoke(Join({"replay", "draw", "--capture", "x.gfxr"}, global)).exit == kUsage, "draw requires a selector");
    Require(Invoke(Join({"replay", "draw", "--capture", "x.gfxr", "--block", "1", "--execution", "2"}, global)).exit == kUsage, "one selector only");
    Require(Code(Invoke(Join({"replay", "frame", "--capture", (root / "none.gfxr").u8string(), "--frame", "1"}, global))) == "capture_not_found", "missing capture");
    Require(Invoke(Join({"shader", "compile", "x.frag", "--stage", "bogus", "--output", "x.spv"}, global)).exit == kUsage, "bad stage is a usage error");

    const auto toolsFile = FindUpwards(fs::current_path(), "local-tools.json");
    const auto capture = FindUpwards(fs::current_path(), "samples/windows-triangle.gfxr");
    if (toolsFile.empty() || capture.empty()) { std::cout << "Replay CLI replay checks skipped (local-tools.json or samples/windows-triangle.gfxr not found)\n"; return; }
    Json localTools; { std::ifstream in(toolsFile); localTools = Json::parse(in); }
    for (const auto* key : {"replay", "convert", "extract", "compiler", "validator"})
        if (!localTools.contains(key) || !fs::exists(fs::u8path(localTools[key].get<std::string>()))) {
            std::cout << "Replay CLI replay checks skipped (tool '" << key << "' unavailable)\n"; return;
        }
    const auto tools = Join(global, {"--tools", toolsFile.u8string()});
    const auto work = root / "replay-cli-work"; fs::create_directories(work);
    const auto cap = capture.u8string();

    // Static shader round trip: export, compile, check.
    const auto exportDir = work / "export";
    const auto exported = Invoke(Join({"shader", "export", "21", "--capture", cap, "--out", exportDir.u8string()}, tools));
    Require(exported.exit == 0, "shader export: " + exported.text);
    for (const auto* file : {"module.spv", "module.glsl", "module.spvasm", "reflection.json", "interface.json", "interface.txt", "stage.json"})
        Require(fs::is_regular_file(exportDir / file), std::string("export wrote ") + file);
    Require(exported.document["result"]["stage"] == "VK_SHADER_STAGE_FRAGMENT_BIT" && exported.document["result"]["editable"] == true, "export stage");
    const auto shown = Invoke(Join({"shader", "show", "21", "--capture", cap, "--format", "reflection"}, tools));
    Require(shown.exit == 0 && shown.document["result"]["reflection"].get<std::string>().find("Output outColor") != std::string::npos, "shader show reflection");
    const auto compiled = Invoke(Join({"shader", "compile", (exportDir / "module.glsl").u8string(), "--stage", "frag", "--output", (work / "c.spv").u8string()}, tools));
    Require(compiled.exit == 0 && fs::file_size(work / "c.spv") > 0, "shader compile: " + compiled.text);
    Require(Invoke(Join({"shader", "check", "21", (work / "c.spv").u8string(), "--capture", cap}, tools)).document["result"]["compatible"] == true, "shader check compatible");

    // Compile error: diagnostics, exit 1, and no replay is launched.
    { std::ofstream bad(work / "bad.frag"); bad << "#version 450\nvoid main() { oops }\n"; }
    const auto badOut = work / "bad-out";
    const auto bad = Invoke(Join({"replay", "frame", "--capture", cap, "--frame", "1", "--override", "21=" + (work / "bad.frag").u8string(), "--out", badOut.u8string()}, tools));
    Require(bad.exit == kFailed && Code(bad) == "compile_failed" && bad.document["error"]["detail"]["diagnostics"].get<std::string>().find("oops") != std::string::npos, "compile error: " + bad.text);
    Require(!HasFiles(badOut, "replay.log") && !HasFiles(badOut, "request.json"), "no replay launched after compile error");

    // Interface rejection: moving the fragment output location fails before replay with both interfaces in the detail.
    { std::ifstream in(exportDir / "module.glsl"); std::string text((std::istreambuf_iterator<char>(in)), {});
      const auto at = text.find("location = 0) out"); Require(at != std::string::npos, "exported glsl has output");
      text.replace(at, 17, "location = 1) out"); std::ofstream out(work / "iface.frag"); out << text; }
    const auto ifaceOut = work / "iface-out";
    const auto iface = Invoke(Join({"replay", "frame", "--capture", cap, "--frame", "1", "--override", "21=" + (work / "iface.frag").u8string(), "--out", ifaceOut.u8string()}, tools));
    Require(iface.exit == kFailed && Code(iface) == "interface_mismatch", "interface mismatch: " + iface.text);
    Require(iface.document["error"]["detail"].contains("original_interface") && iface.document["error"]["detail"].contains("candidate_interface"), "interface detail");
    Require(!HasFiles(ifaceOut, "replay.log"), "no replay launched after interface mismatch");
    Require(Code(Invoke(Join({"shader", "check", "21", (work / "iface.frag").u8string(), "--capture", cap}, tools))) == "interface_mismatch", "shader check mismatch");
    Require(Code(Invoke(Join({"replay", "frame", "--capture", cap, "--frame", "1", "--override", "999=" + (work / "c.spv").u8string(), "--out", (work / "nomod").u8string()}, tools))) == "module_not_found", "unknown module");

    // Override confirmation failure: a module id the backend never loads is not logged as "Replacement shader found", so the
    // shared replay core fails and the CLI maps that message to override_not_confirmed (see RunReplay in commands_replay.cpp).
    {
        std::map<std::string, std::string> map; for (const auto& [key, value] : localTools.items()) if (value.is_string()) map[key] = value.get<std::string>();
        ReplayRequest request; request.capture = capture; request.output = work / "unconfirmed"; request.frame = 1; request.optimize = false;
        request.overrides[999999] = work / "c.spv";
        bool failed = false, named = false;
        try { ReplayCapture(request, {}, map); } catch (const std::exception& error) { failed = true; named = std::string(error.what()).find("did not confirm replacement of module sh999999") != std::string::npos; }
        Require(failed && named, "override confirmation failure");
    }

    // Real desktop replays.
    const auto frameOut = work / "frame";
    const auto frame = Invoke(Join({"replay", "frame", "--capture", cap, "--frame", "1", "--out", frameOut.u8string()}, tools));
    Require(frame.exit == 0, "frame replay: " + frame.text);
    const auto& f = frame.document["result"];
    Require(f["cache_hit"] == false && f["target"]["android"] == false && fs::is_regular_file(fs::u8path(f["image"].get<std::string>())) && fs::is_regular_file(fs::u8path(f["image_png"].get<std::string>())) &&
        fs::is_regular_file(fs::u8path(f["manifest"].get<std::string>())) && fs::is_regular_file(fs::u8path(f["replay_log"].get<std::string>())), "frame artifacts");
    const auto png = LoadImage(fs::u8path(f["image_png"].get<std::string>())); Require(png.width > 0 && png.height > 0, "frame png");
    const auto cached = Invoke(Join({"replay", "frame", "--capture", cap, "--frame", "1", "--out", (work / "frame2").u8string()}, tools));
    Require(cached.exit == 0 && cached.document["result"]["cache_hit"] == true, "second frame replay hits the shared result cache");
    const auto cmp = Invoke(Join({"compare", f["image_png"].get<std::string>(), cached.document["result"]["image_png"].get<std::string>()}, tools));
    Require(cmp.exit == 0 && cmp.document["result"]["identical"] == true, "cached replay identical");

    const auto draw = Invoke(Join({"replay", "draw", "--capture", cap, "--block", "82", "--out", (work / "draw").u8string()}, tools));
    Require(draw.exit == 0, "draw replay: " + draw.text);
    const auto& r = draw.document["result"];
    Require(r["draw"]["block"] == 82 && r["outputs"].size() >= 1 && fs::is_regular_file(fs::u8path(r["outputs"][0]["png"].get<std::string>())) && r["outputs"][0]["metadata"]["category"] == "output", "draw outputs");
    Require(Code(Invoke(Join({"replay", "draw", "--capture", cap, "--block", "1"}, tools))) == "draw_not_found", "draw not found");
    Require(Invoke(Join({"replay","draw","--capture",cap,"--execution","0","--subdraw","1"},tools)).exit==kUsage,"subdraw rejects ordinary direct draws before replay");
    // Native public-sample diagnostics: outline preserves original interiors and is thinner than solid.
    auto diagnostic=[&](const char* mode,const char* name,std::vector<std::string> extra=std::vector<std::string>{}){
        auto args=Join({"replay","draw","--capture",cap,"--execution","0","--visualize",mode,"--out",(work/name).u8string()},extra);
        return Invoke(Join(args,tools));
    };
    const auto solid=diagnostic("solid","solid"),outline=diagnostic("outline","outline");
    Require(solid.exit==0&&outline.exit==0,"solid/outline diagnostics: "+outline.text);
    Require(outline.document["result"]["visualization"]=="outline"&&outline.document["result"]["diagnostic_sources"]["baseline"].contains("original"),"outline source provenance");
    auto imageOf=[](const Outcome& result){return LoadImage(fs::u8path(result.document["result"]["image_png"].get<std::string>()));};
    auto orange=[](const Image& image){size_t count=0;for(size_t p=0;p<image.rgba.size();p+=4)if(image.rgba[p]>250&&image.rgba[p+1]>100&&image.rgba[p+1]<240&&image.rgba[p+2]<5)++count;return count;};
    const auto filled=imageOf(solid),edged=imageOf(outline),original=imageOf(draw);
    Require(orange(edged)>0&&orange(edged)<orange(filled)/5,"outline is a thin boundary");
    const auto center=(size_t(edged.height/2)*edged.width+edged.width/2)*4;
    Require(std::equal(edged.rgba.begin()+center,edged.rgba.begin()+center+4,original.rgba.begin()+center),"outline interior retains original pixels");
    const auto noUv=diagnostic("uv","no-uv");Require(noUv.exit==kFailed&&Code(noUv)=="visualization_unsupported","unavailable UV reports a capability error");
    // A compatible replacement exposes a UV semantic; generation must use that effective module.
    {std::ifstream in(exportDir/"module.glsl");std::string text((std::istreambuf_iterator<char>(in)),{});size_t pos=0;
     while((pos=text.find("fragColor",pos))!=std::string::npos){text.replace(pos,9,"uv");pos+=2;}std::ofstream(work/"uv.frag")<<text;}
    const auto uv=diagnostic("uv","uv",{"--override","21="+(work/"uv.frag").u8string()});
    Require(uv.exit==0,"UV generated from override: "+uv.text);
    Require(uv.document["result"]["diagnostic_sources"]["modified"]["uv"]["detail"].get<std::string>().find("uv")!=std::string::npos,"UV input metadata");
    const auto checker=imageOf(uv);size_t light=0,dark=0;
    for(size_t p=0;p<checker.rgba.size();p+=4)if(checker.rgba[p]==checker.rgba[p+1]&&checker.rgba[p+1]==checker.rgba[p+2]){light+=checker.rgba[p]>200;dark+=checker.rgba[p]>0&&checker.rgba[p]<100;}
    Require(light>100&&dark>100,"UV renders a checkerboard");
    // --draw <dump draw dir>: capture and selector come from the dump.
    const auto dumpDir = work / "dump"; const auto drawDir = dumpDir / "frames/0001/passes/001-renderpass-b0/draws/0000-b82-vkCmdDraw";
    fs::create_directories(drawDir);
    { std::ofstream(dumpDir / "manifest.json") << Json{{"schema", kDumpSchema}, {"capture", {{"path", cap}, {"sha256", f["capture"]["sha256"]}}}}.dump();
      std::ofstream(drawDir / "draw.json") << Json{{"execution", 0}, {"block", 82}, {"occurrence", 0}}.dump(); }
    const auto fromDump = Invoke(Join({"replay", "draw", "--draw", drawDir.u8string(), "--out", (work / "draw-dump").u8string()}, tools));
    Require(fromDump.exit == 0 && fromDump.document["result"]["draw"]["block"] == 82 && fromDump.document["result"]["cache_hit"] == true, "replay draw from dump path: " + fromDump.text);

    // Override with baseline comparison: the swapped colors differ in red/blue only; the backend confirmed the replacement.
    { std::ifstream in(exportDir / "module.glsl"); std::string text((std::istreambuf_iterator<char>(in)), {});
      const auto at = text.find("vec4(fragColor, 1.0)"); Require(at != std::string::npos, "exported glsl body");
      text.replace(at, 20, "vec4(fragColor.bgr, 1.0)"); std::ofstream out(work / "swap.frag"); out << text; }
    const auto base = Invoke(Join({"replay", "frame", "--capture", cap, "--frame", "1", "--override", "21=" + (work / "swap.frag").u8string(), "--baseline", "--out", (work / "baseline").u8string()}, tools));
    Require(base.exit == 0, "baseline override replay: " + base.text);
    const auto& b = base.document["result"];
    Require(b["overrides"][0]["module"] == 21 && b["overrides"][0]["confirmed"] == true && b["runs"].contains("baseline") && b["runs"].contains("modified"), "baseline result");
    Require(b["comparison"]["identical"] == false && b["comparison"]["max_abs_diff"]["g"] == 0 && b["comparison"]["max_abs_diff"]["r"].get<int>() > 100 && fs::is_regular_file(fs::u8path(b["comparison"]["diff_image"].get<std::string>())), "baseline comparison: " + b["comparison"].dump());
    const auto outlinedPair=diagnostic("outline","outline-pair",{"--override","21="+(work/"swap.frag").u8string(),"--baseline"});
    Require(outlinedPair.exit==0,"outline baseline/modified: "+outlinedPair.text);
    Require(outlinedPair.document["result"]["comparison"]["identical"]==false&&outlinedPair.document["result"]["diagnostic_sources"].contains("baseline")&&outlinedPair.document["result"]["diagnostic_sources"].contains("modified"),"outline uses each variant's original pixels and override set");
    const auto indirect=FindUpwards(fs::current_path(),"samples/indirect-triangles.gfxr");
    if(!indirect.empty()){
        const auto single=Invoke(Join({"replay","draw","--capture",indirect.u8string(),"--execution","0","--subdraw","1","--visualize","outline","--out",(work/"indirect-outline").u8string()},tools));
        Require(single.exit==0,"indirect outline: "+single.text);
        Require(single.document["result"]["subdraw"]==1&&single.document["result"]["runs"]["baseline"]["draw_parameters"]["indirectParams"].size()==1&&orange(imageOf(single))>0,"selected record outline and actual indirect arguments");
    }
    // Draw-scoped override + validation: validation either reports or maps a missing layer to exit 4 with status unavailable.
    const auto validated = Invoke(Join({"replay", "frame", "--capture", cap, "--frame", "1", "--validate", "--out", (work / "validated").u8string()}, tools));
    if (validated.exit == 0) Require(validated.document["result"]["validation"]["status"].is_string() && fs::is_regular_file(fs::u8path(validated.document["result"]["validation"]["report"].get<std::string>())), "validation report");
    else Require(validated.exit == kMissingTool && Code(validated) == "validation_unavailable" && validated.document["error"]["detail"]["validation"]["status"] == "unavailable", "validation unavailable: " + validated.text);
    // Perf: fresh runs with a measurement range, result cache never used.
    const auto perf = Invoke(Join({"perf", "--capture", cap, "--frames", "2-3", "--runs", "2", "--out", (work / "perf").u8string()}, tools));
    Require(perf.exit == 0, "perf: " + perf.text);
    const auto& p = perf.document["result"];
    Require(p["runs"].size() == 2 && p["result_cache_used"] == false && p["frames"]["upstream_range"] == "2-4", "perf runs");
    Require(p["summary"]["frame_time_ms"]["min"].get<double>() > 0 && p["summary"]["frame_time_ms"]["max"].get<double>() >= p["summary"]["frame_time_ms"]["median"].get<double>() &&
        p["summary"]["fps"]["median"].get<double>() > 0 && p["runs"][0]["load_seconds"].is_number() && p["runs"][0]["frames_measured"] == 2, "perf metrics");
    Require(fs::is_regular_file(fs::u8path(p["runs"][0]["measurement_file"].get<std::string>())) && p["runs"][0]["cache_hit"] == false, "perf measurement file, no result cache");
    std::cout << "Replay CLI checks passed\n";
}
} // namespace gfx::tests
