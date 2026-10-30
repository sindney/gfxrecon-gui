#include "analysis/capture.h"
#include "analysis/mesh.h"
#include "analysis/image_orientation.h"
#include "analysis/image_display.h"
#include "analysis/image_details.h"
#include "analysis/image_readback_layout.h"
#include "analysis/depth_sample.h"
#include "platform/process.h"
#include "shaders/syntax.h"
#include "shaders/interface.h"
#include "shaders/metadata.h"
#include "shaders/diagnostics.h"
#include "shaders/decode.h"
#include "replay/replay.h"
#include "platform/android_display.h"
#include "capture/capture.h"
#include "core/artifacts.h"
#include "core/image_io.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <cstring>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef LoadImage
#include <shellapi.h>
#include <io.h>
#include <fcntl.h>
#endif

#define CHECK(x) do { if (!(x)) throw std::runtime_error("Check failed: " #x); } while (false)
namespace gfx::tests {
void ProfileReplay(const std::filesystem::path&,const std::filesystem::path&,const std::string&,const std::filesystem::path&,const std::filesystem::path&,bool);
void DumpFeasibility(const std::filesystem::path&,const std::filesystem::path&,const std::string&,const std::filesystem::path&,const std::filesystem::path&,uint32_t,const std::string&);
void RunAnalysisTests(const std::filesystem::path& root);
void RunCacheTests(const std::filesystem::path& root);
void RunPipelineCacheTests(const std::filesystem::path& root);
void RunOptimizerTests(const std::filesystem::path& root);
void RunCaptureSaveTests(const std::filesystem::path& root);
void RunDeviceTests(const std::filesystem::path& root);
void RunAndroidCaptureTests(const std::filesystem::path& root);
void RunMeshTests(const std::filesystem::path& root);
void RunLiveCaptureTests(const std::string&,const std::string&,const std::string&,const std::string&,const std::filesystem::path&);
void BenchmarkIndex(const std::filesystem::path& root);
void RunExportTests(const std::filesystem::path& root);
void RunImageDecodeTests(const std::filesystem::path& root);
void RunCliTests(const std::filesystem::path& root);
void RunReplayCliTests(const std::filesystem::path& root);
}
namespace {
using Color = TextEditor::PaletteIndex;
void CheckToken(const std::string& s, bool spirv, Color expected, size_t length = 0) {
    const char* first = nullptr; const char* last = nullptr; Color c{};
    CHECK((spirv ? gfx::TokenizeSpirv : gfx::TokenizeGlsl)(s.data(), s.data() + s.size(), first, last, c));
    CHECK(first == s.data()); CHECK(last > first); CHECK(last <= s.data() + s.size()); CHECK(c == expected);
    if (length) CHECK(static_cast<size_t>(last - first) == length);
}
void SyntaxTests() {
    CHECK(gfx::GlslLanguage().mTokenRegexStrings.empty());
    CHECK(gfx::SpirvLanguage().mTokenRegexStrings.empty());
    CHECK(gfx::SpirvLanguage().mSingleLineComment == ";");
    CHECK(gfx::GlslLanguage().mCommentStart == "/*");
    CheckToken("layout", false, Color::Keyword);
    CheckToken("vec4", false, Color::Keyword);
    CheckToken("mat3x4", false, Color::Keyword);
    CheckToken("material", false, Color::Identifier);
    CheckToken("vec4offset", false, Color::Identifier);
    CheckToken("gl_Position", false, Color::KnownIdentifier);
    CheckToken("texture", false, Color::KnownIdentifier);
    CheckToken("1e-3+2", false, Color::Number, 4);
    CheckToken("0x1.fp+3", false, Color::Number, 8);
    CheckToken("# version 460", false, Color::Preprocessor, 9);
    CheckToken("// comment", false, Color::Comment);
    CheckToken("/* comment */ int", false, Color::MultiLineComment, 13);
    CheckToken("; OpName %id", true, Color::Comment);
    CheckToken("%1000 =", true, Color::Identifier, 5);
    CheckToken("OpEntryPoint", true, Color::Keyword);
    CheckToken("Fragment", true, Color::KnownIdentifier);
    CheckToken("\"name\\\"quoted\" tail", true, Color::String, 14);
    CheckToken("-0x1p+0", true, Color::Number);
    const auto start = std::chrono::steady_clock::now();
    size_t tokens = 0;
    std::string document;
    for (int i = 0; i < 10000; ++i) document += "layout(location = 0) out vec4 color; color = vec4(1.0e-3) + gl_FragCoord; // test\n";
    size_t offset = 0;
    while (offset < document.size()) {
        const auto newline = document.find('\n', offset);
        const char* cursor = document.data() + offset;
        const char* lineEnd = document.data() + newline;
        while (cursor != lineEnd) {
            const char *first, *last; Color c;
            if (!gfx::TokenizeGlsl(cursor, lineEnd, first, last, c)) break;
            CHECK(last > cursor); cursor = last; ++tokens;
        }
        offset = newline + 1;
    }
    auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << "GLSL: 10000 lines, " << document.size() << " bytes, " << tokens << " tokens, " << ms << " ms\n";
    CHECK(tokens > 100000);
}
void MetadataTests(const std::filesystem::path& dir) {
    const auto binary=dir/"draw-index.spv";
    auto writeWords=[&](std::initializer_list<uint32_t> code){std::ofstream out(binary,std::ios::binary);for(const auto word:code)out.write(reinterpret_cast<const char*>(&word),4);};
    writeWords({0x07230203,0x00010300,0,20,0,(4u<<16)|71,10,11,4426});CHECK(gfx::ShaderUsesDrawIndex(binary));
    writeWords({0x07230203,0x00010300,0,20,0,(5u<<16)|72,10,0,11,4426});CHECK(gfx::ShaderUsesDrawIndex(binary));
    writeWords({0x07230203,0x00010300,0,20,0,(4u<<16)|71,10,11,42});CHECK(!gfx::ShaderUsesDrawIndex(binary));
    writeWords({0x07230203,0x00010300,0,20,0,(4u<<16)|71,10});bool malformed=false;
    try{gfx::ShaderUsesDrawIndex(binary);}catch(...){malformed=true;}CHECK(malformed);std::filesystem::remove(binary);
    std::vector<uint32_t> words{0x07230203, 0x00010300, 0, 100, 0};
    auto append = [&](uint32_t op, std::vector<uint32_t> operands, const std::string& text) {
        auto count = (text.size() + 1 + 3) / 4; const auto first = operands.size(); operands.resize(first + count, 0);
        for (size_t i = 0; i < text.size(); ++i) operands[first + i / 4] |= uint32_t(static_cast<unsigned char>(text[i])) << ((i % 4) * 8);
        words.push_back((static_cast<uint32_t>(operands.size() + 1) << 16) | op); words.insert(words.end(), operands.begin(), operands.end());
    };
    append(7, {10}, "shader.frag");
    append(3, {2, 450, 10}, "#version 450\nvoid main()");
    append(2, {}, "{}\n");
    words.insert(words.end(), {(4u << 16) | 8, 10, 2, 5});
    append(11, {11}, "NonSemantic.Shader.DebugInfo.100");
    auto result = gfx::ReadShaderMetadata(words);
    CHECK(result.capturedSource.find("void main(){}") != std::string::npos);
    CHECK(result.debug["sources"][0]["file"] == "shader.frag");
    CHECK(result.debug["line_mappings"][0]["line"] == 2);
    CHECK(!result.debug["source_stepping_available"].get<bool>());
    const auto stripped = gfx::ReadShaderMetadata({0x07230203, 0x00010300, 0, 1, 0});
    CHECK(stripped.capturedSource.empty() && !stripped.debug["has_embedded_source"].get<bool>());
    words.push_back(0); bool rejected = false;
    try { gfx::ReadShaderMetadata(words); } catch (...) { rejected = true; }
    CHECK(rejected); words.back() = (50u << 16) | 3; rejected = false;
    try { gfx::ReadShaderMetadata(words); } catch (...) { rejected = true; }
    CHECK(rejected);
}
void CaptureTests(const std::filesystem::path& dir) {
    const auto file = dir / "fixture.jsonl";
    {
        std::ofstream f(file);
        f << R"({"header":{"gfxrecon-version":"fixture"}})" << '\n';
        f << R"({"index":4294967297,"function":{"name":"vkBeginCommandBuffer","args":{"commandBuffer":42}}})" << '\n';
        f << R"({"index":4294967298,"function":{"name":"vkCmdBindPipeline","args":{"commandBuffer":42,"pipelineBindPoint":"VK_PIPELINE_BIND_POINT_GRAPHICS","pipeline":9007199254740993}}})" << '\n';
        f << R"({"index":4294967299,"function":{"name":"vkCmdDraw","args":{"commandBuffer":42,"vertexCount":3}}})" << '\n';
        f << R"({"index":4294967300,"function":{"name":"vkBeginCommandBuffer","args":{"commandBuffer":42}}})" << '\n';
        f << R"({"index":4294967301,"function":{"name":"vkCmdDraw","args":{"commandBuffer":42}}})" << '\n';
    }
    auto capture = gfx::ReadCapture(file);
    CHECK(capture->events.size() == 5);
    CHECK(capture->events[2].pipeline == 9007199254740993ull);
    CHECK(capture->events[4].pipeline == 0);
    CHECK(capture->ReadEvent(2)["function"]["args"]["vertexCount"] == 3);
    {
        std::ofstream f(file, std::ios::trunc);
        f << R"({"index":1,"function":{"name":"vkBeginCommandBuffer","args":{"commandBuffer":10}}})" << '\n';
        f << R"({"index":2,"function":{"name":"vkCmdBeginRendering","args":{"commandBuffer":10}}})" << '\n';
        f << R"({"index":3,"function":{"name":"vkCmdDraw","args":{"commandBuffer":10}}})" << '\n';
        f << R"({"index":4,"function":{"name":"vkCmdEndRendering","args":{"commandBuffer":10}}})" << '\n';
        f << R"({"index":5,"function":{"name":"vkQueueSubmit","args":{"pSubmits":[{"pCommandBuffers":[10]}]}}})" << '\n';
        f << R"({"index":6,"function":{"name":"vkQueuePresentKHR","args":{}}})" << '\n';
        f << R"({"frame":{"marker_type":"EndMarker","frame_number":1}})" << '\n';
        f << R"({"index":7,"function":{"name":"vkQueueSubmit2","args":{"pSubmits":[{"pCommandBufferInfos":[{"commandBuffer":10}]}]}}})" << '\n';
        f << R"({"index":8,"function":{"name":"vkBeginCommandBuffer","args":{"commandBuffer":10}}})" << '\n';
        f << R"({"index":9,"function":{"name":"vkCmdDispatch","args":{"commandBuffer":10}}})" << '\n';
        f << R"({"index":10,"function":{"name":"vkQueueSubmit","args":{"pSubmits":[{"pCommandBuffers":[10]}]}}})" << '\n';
    }
    capture = gfx::ReadCapture(file);
    CHECK(capture->executions.size() == 3);
    CHECK(capture->executions[0].event == capture->executions[1].event);
    CHECK(capture->executions[0].submit == 5 && capture->executions[1].submit == 7);
    CHECK(capture->executions[1].frame == 2 && capture->executions[1].begin == 1);
    CHECK(capture->executions[2].begin == 8 && capture->executions[2].generation == 2);
    CHECK(capture->DumpRequest(0)["RenderPass"] == gfx::Json::array({gfx::Json::array({gfx::Json::array({2,4})})}));
    CHECK(capture->DumpRequest(2)["Dispatch"] == gfx::Json::array({gfx::Json::array({9})}));
    { std::ofstream f(file, std::ios::app); f << "{truncated"; }
    bool rejected = false;
    try { gfx::ReadCapture(file); } catch (...) { rejected = true; }
    CHECK(rejected);
    {
        std::ofstream f(file, std::ios::trunc);
        f << R"({"index":1,"function":{"name":"vkCreateDevice","args":{"pCreateInfo":{"ppEnabledExtensionNames":["VK_ANDROID_external_memory_android_hardware_buffer"]}}}})" << '\n';
    }
    CHECK(gfx::ReadCapture(file)->android);
    {
        std::ofstream f(file, std::ios::trunc);
        f << R"({"index":1,"function":{"name":"vkCreateAndroidSurfaceKHR","args":{}}})" << '\n';
    }
    CHECK(gfx::ReadCapture(file)->android);
    {
        std::ofstream f(file, std::ios::trunc);
        f << R"({"index":1,"function":{"name":"vkCreateWin32SurfaceKHR","args":{}}})" << '\n';
    }
    CHECK(!gfx::ReadCapture(file)->android);
    for (const auto length : {4094, 4095, 4096, 8190}) for (bool newline : {false, true}) {
        const auto record = gfx::Json{{"index", 1}, {"function", {{"name", "vkFixture"}, {"args", {{"text", std::string(length, 'x')}}}}}};
        { std::ofstream out(file, std::ios::binary | std::ios::trunc); out << record.dump(); if (newline) out << '\n'; }
        const auto bounded = gfx::ReadCapture(file);
        CHECK(bounded->events.size() == 1 && bounded->ReadEvent(0) == record);
    }
    { std::ofstream out(file, std::ios::binary | std::ios::trunc); out << std::string(64 * 1024 * 1024 + 1, ' '); }
    rejected = false; try { gfx::ReadCapture(file); } catch (const std::exception& error) { rejected = std::string(error.what()).find("64 MiB") != std::string::npos; }
    CHECK(rejected);
}
void ReplayIdentityTests(const std::filesystem::path& root) {
    const auto source = root / "replay.gfxr", output = root / "replay-output";
    { std::ofstream file(source); file << "original"; }
    gfx::ReplayRequest request; request.capture = source; request.output = output;
    request.expectedCaptureHash = gfx::Sha256File(source);
    { std::ofstream file(source); file << "modified"; }
    bool rejected = false;
    try { gfx::ReplayCapture(request, {}, {}); } catch (const std::exception& error) { rejected = std::string(error.what()).find("changed after import") != std::string::npos; }
    CHECK(rejected && !std::filesystem::exists(output));
    request.expectedCaptureHash = gfx::Sha256File(source);
    try { gfx::ReplayCapture(request, {}, {}); } catch (...) {}
    CHECK(std::filesystem::is_regular_file(output / "failure.json"));
    CHECK(!std::filesystem::exists(output / "manifest.json"));
    { std::ifstream in(output / "failure.json"); const auto failure = gfx::Json::parse(in); CHECK(!failure.at("complete").get<bool>()); CHECK(failure.at("capture_sha256") == request.expectedCaptureHash); }
    rejected = false;
    try { gfx::ReplayCapture(request, {}, {}); } catch (const std::exception& error) { rejected = std::string(error.what()).find("new, empty") != std::string::npos; }
    CHECK(rejected);
    std::filesystem::remove_all(output); std::filesystem::remove(source);
}
void ReplayTargetTests() {
    const auto devices = gfx::ParseAdbDevices("* daemon started successfully *\r\nList of devices attached\r\nphone-a device product:art model:Art\r\nphone-b unauthorized\r\nphone-c offline\r\n");
    CHECK(devices.size() == 3 && devices[0].serial == "phone-a");
    CHECK(!gfx::SelectReplayTarget(gfx::ReplayMode::Automatic, false, "", devices).android);
    CHECK(!gfx::SelectReplayTarget(gfx::ReplayMode::Local, true, "", {}).android);
    CHECK(gfx::SelectReplayTarget(gfx::ReplayMode::Automatic, true, "", devices).serial == "phone-a");
    CHECK(gfx::SelectReplayTarget(gfx::ReplayMode::Android, false, "phone-a", devices).android);
    auto rejects = [&](const std::string& serial, const std::vector<gfx::AndroidDevice>& list) {
        try { gfx::SelectReplayTarget(gfx::ReplayMode::Automatic, true, serial, list); return false; }
        catch (const std::runtime_error&) { return true; }
    };
    CHECK(rejects("", {}));
    CHECK(rejects("phone-b", devices));
    CHECK(rejects("missing-device", devices));
    CHECK(rejects("", {{"one", "device", ""}, {"two", "device", ""}}));
    CHECK(gfx::QuoteAndroidArgument("one' two;$()") == "'one'\"'\"' two;$()'");
}
void CaptureControlTests() {
    for (const auto& valid : {"1-3", "1", "10,20-25", "1-3,5,7-9"}) gfx::ValidateCaptureFrames(valid);
    for (const auto& invalid : {"", "0", "4-2", "1-3,3-4", "2,1", "1,", "1;echo", "-1", "1-2-3"}) {
        bool rejected = false; try { gfx::ValidateCaptureFrames(invalid); } catch (...) { rejected = true; }
        CHECK(rejected);
    }
    CHECK(gfx::PlanRestore("null", "target", "target") == gfx::RestoreDecision::Restore);
    CHECK(gfx::PlanRestore("old", "target", "old") == gfx::RestoreDecision::AlreadyRestored);
    CHECK(gfx::PlanRestore("old", "target", "another-session") == gfx::RestoreDecision::Conflict);
    CHECK(gfx::PlanRestore("", "1-3", "1-3") == gfx::RestoreDecision::Restore);
    CHECK(gfx::SplitProcessArguments("triangle --name \"a b\" \"$(x);&\"") == std::vector<std::string>({"triangle", "--name", "a b", "$(x);&"}));
}
void BinaryTransferTest(const std::string& self, const std::filesystem::path& dir) {
    const std::string bytes(9 * 1024 * 1024 + 7, '\xA5');
    const auto input = dir / "input.bin", output = dir / "output.bin";
    { std::ofstream f(input, std::ios::binary); f.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); }
    gfx::ProcessRequest request{self, {"--copy-stream"}};
    request.stdinFile = input; request.stdoutFile = output;
    auto result = gfx::RunProcess(request);
    CHECK(result.Success()); CHECK(result.output == "copy-ok");
    CHECK(std::filesystem::file_size(output) == bytes.size());
    std::ifstream file(output, std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(file), {}) == bytes);
    file.close(); std::filesystem::remove(input); std::filesystem::remove(output);
}
void ResourceTests(const std::filesystem::path& dir) {
    std::vector<gfx::ResourceArtifact> orderedImages{
        {"00-input.png",0,"",{{"category","input"},{"aspect","COLOR"},{"location",0}}},
        {"01-stencil.bmp",0,"",{{"category","output"},{"aspect","STENCIL"}}},
        {"02-depth.bmp",0,"",{{"category","output"},{"aspect","DEPTH"}}},
        {"03-color1.bmp",0,"",{{"category","output"},{"aspect","COLOR"},{"location",1}}},
        {"04-color0-mip1.bmp",0,"",{{"category","output"},{"aspect","COLOR"},{"location",0},{"mipLevel",1}}},
        {"99-color0.bmp",0,"",{{"category","output"},{"aspect","COLOR"},{"location",0},{"mipLevel",0}}}};
    CHECK(gfx::PrimaryOutputImage(orderedImages)==std::filesystem::path("99-color0.bmp"));
    gfx::OutputSelection output; output.aspect="COLOR"; output.location=1;
    CHECK(gfx::SelectOutputImage(orderedImages,output)==std::filesystem::path("03-color1.bmp"));
    output.location=0; output.mip=1;
    CHECK(gfx::SelectOutputImage(orderedImages,output)==std::filesystem::path("04-color0-mip1.bmp"));
    output.layer=9; CHECK(gfx::SelectOutputImage(orderedImages,output).empty());
    orderedImages.resize(3);CHECK(gfx::PrimaryOutputImage(orderedImages)==std::filesystem::path("02-depth.bmp"));
    orderedImages.resize(1);CHECK(gfx::PrimaryOutputImage(orderedImages).empty());
    CHECK(gfx::DescriptorUsesSampler(0));CHECK(gfx::DescriptorUsesSampler(1));CHECK(!gfx::DescriptorUsesSampler(2));
    CHECK(!gfx::DescriptorUsesSampler("VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE"));CHECK(gfx::DescriptorUsesSampler("VK_DESCRIPTOR_TYPE_SAMPLER"));
    gfx::Capture capture;gfx::Execution execution;
    capture.objects[1]={"DescriptorSetLayout","",0,0,gfx::Json::parse(R"({"pBindings":[{"binding":0,"descriptorType":2,"stageFlags":16},{"binding":1,"descriptorType":1,"stageFlags":16},{"binding":2,"descriptorType":0,"stageFlags":16,"pImmutableSamplers":[7]},{"binding":3,"descriptorType":0,"stageFlags":1}]})")};
    execution.descriptorValues=gfx::Json::parse(R"({"0":{"layout":1,"bindings":{"0":{"0":{"type":2,"value":{"sampler":999}}},"1":{"0":{"type":1,"value":{"sampler":8}}},"2":{"0":{"type":0,"value":{"sampler":999}}},"3":{"0":{"type":0,"value":{"sampler":9}}}}}})");
    const auto fragment=gfx::CollectStageSamplers(capture,execution,{0,"VK_SHADER_STAGE_FRAGMENT_BIT","main"});
    CHECK(fragment.size()==2);CHECK(fragment[0].sampler==8&&!fragment[0].immutable);CHECK(fragment[1].sampler==7&&fragment[1].immutable);
    const auto vertex=gfx::CollectStageSamplers(capture,execution,{0,"VK_SHADER_STAGE_VERTEX_BIT","main"});CHECK(vertex.size()==1&&vertex[0].sampler==9);
    const auto folder = dir / "resources"; std::filesystem::create_directory(folder);
    std::vector<uint8_t> data(8); const float f = 1.5f; const uint32_t u = 0xaabbccddu;
    std::memcpy(data.data(), &f, 4); std::memcpy(data.data() + 4, &u, 4);
    { std::ofstream out(folder / "buffer.bin", std::ios::binary); out.write(reinterpret_cast<const char*>(data.data()), data.size()); }
    CHECK(gfx::DecodeResourceValue(data, 0, 4) == "1.5");
    CHECK(gfx::DecodeResourceValue(data, 4, 2) == "2864434397");
    CHECK(gfx::DecodeResourceValue(data, 4, 5) == "-");
    const auto page = gfx::ReadResourcePage(folder / "buffer.bin", 4, 4);
    CHECK(page.bytes.size() == 4 && page.totalSize == 8 && gfx::DecodeResourceValue(page.bytes, 0, 2) == "2864434397");
    bool rejected = false; try { gfx::ReadResourcePage(folder / "buffer.bin", 9); } catch (...) { rejected = true; }
    CHECK(rejected);
    { std::ofstream image(folder / "image.bmp", std::ios::binary); image << "fixture"; }
    { std::ofstream meta(folder / "fixture_dr.json"); meta << R"([{"drawCallCommands":[{"drawIndex":143,"colorAttachments":[{"imageId":16,"format":"VK_FORMAT_R8G8B8A8_SRGB","subresources":[{"file":"/remote/image.bmp","mipLevel":0}]}]}]}])"; }
    const auto resources = gfx::CollectResources(folder);
    CHECK(resources.size() == 2);
    const auto found = std::find_if(resources.begin(), resources.end(), [](const auto& artifact) { return artifact.file.filename() == "image.bmp"; });
    CHECK(found != resources.end() && found->metadata.at("drawIndex") == 143 && found->metadata.at("imageId") == 16);
    CHECK(found->metadata.at("format") == "VK_FORMAT_R8G8B8A8_SRGB");
    CHECK(found->metadata.at("category")=="output");
    {std::ofstream meta(folder/"inputs_dr.json");meta<<R"([{"drawCallCommands":[{"drawIndex":143,"descriptors":{"fragment":[{"set":1,"binding":2,"arrayIndex":0,"imageId":16,"file":"/remote/image.bmp"},{"set":1,"binding":3,"arrayIndex":0,"imageId":16,"file":"/remote/image.bmp"}]}}]}])";}
    const auto slots=gfx::CollectResources(folder);CHECK(slots.size()==4);
    CHECK(std::count_if(slots.begin(),slots.end(),[](const auto& x){return x.metadata.value("category","")=="input"&&x.metadata.value("stage","")=="fragment";})==2);
    for (const auto& name : {"buffer.bin", "image.bmp", "fixture_dr.json", "inputs_dr.json"}) std::filesystem::remove(folder / name);
    std::filesystem::remove(folder);
}
void ProcessTests(const std::string& self) {
    const std::vector<std::string> args = {"--echo", "", "white space", "quote\"slash\\", "trailing\\", "$(not-a-command);&", u8"着色器"};
    gfx::ProcessRequest r{self, args};
    r.environment["GFX_GUI_TEST_VALUE"] = u8"shader path 着色器";
    const auto output = gfx::RunProcess(r);
    if (!output.Success()) std::cerr << "Child exit " << output.exitCode << ": " << output.output << '\n';
    CHECK(output.Success());
    const auto result = gfx::Json::parse(output.output);
    CHECK(result["args"] == gfx::Json(std::vector<std::string>(args.begin() + 1, args.end())));
    CHECK(result["env"] == r.environment.at("GFX_GUI_TEST_VALUE"));
    CHECK(std::getenv("GFX_GUI_TEST_VALUE") == nullptr);
    for (int i = 0; i < 10; ++i) {
        gfx::ProcessRequest fast{self, {"--echo", std::to_string(i)}};
        const auto completed = gfx::RunProcess(fast);
        CHECK(completed.Success());
        if (completed.output.empty()) throw std::runtime_error("Missing output from rapid child " + std::to_string(i));
        CHECK(gfx::Json::parse(completed.output)["args"][0] == std::to_string(i));
    }
    r.arguments = {"--sleep"}; r.timeout = std::chrono::milliseconds(100);
    CHECK(gfx::RunProcess(r).timedOut);
    r.timeout = std::chrono::seconds(10);
    std::atomic_bool activeCancel{false};
    auto running = std::async(std::launch::async, [&] { return gfx::RunProcess(r, &activeCancel); });
    std::this_thread::sleep_for(std::chrono::milliseconds(40)); activeCancel = true;
    CHECK(running.get().cancelled);
    std::atomic_bool cancel{true};
    CHECK(gfx::RunProcess(r, &cancel).cancelled);
    r.arguments = {"--echo", std::string(10000, 'a')}; r.outputLimit = 100;
    CHECK(gfx::RunProcess(r).outputTruncated);
}
}
int main(int argc, char** argv) {
#ifdef _WIN32
    // The Windows CRT narrow argv uses the local code page; process APIs and
    // the application's path contract use UTF-8 instead.
    int wideCount = 0;
    auto wide = CommandLineToArgvW(GetCommandLineW(), &wideCount);
    std::vector<std::string> utf8;
    std::vector<char*> pointers;
    for (int i = 0; i < wideCount; ++i) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, s.data(), n, nullptr, nullptr);
        s.pop_back(); utf8.push_back(std::move(s));
    }
    LocalFree(wide);
    for (auto& s : utf8) pointers.push_back(s.data());
    argc = wideCount; argv = pointers.data();
#endif
    if(argc==3 && std::getenv("GFXRECON_GUI_TEST_OPTIMIZER") && std::filesystem::is_regular_file(std::filesystem::u8path(argv[1]))) {
        const std::string mode=std::getenv("GFXRECON_GUI_TEST_OPTIMIZER");
        if(const auto count=std::getenv("GFXRECON_GUI_TEST_OPTIMIZER_COUNTER")){std::ofstream out(std::filesystem::u8path(count),std::ios::app);out<<'x';}
        if(mode=="sleep")std::this_thread::sleep_for(std::chrono::seconds(8));
        if(mode=="copy")std::filesystem::copy_file(std::filesystem::u8path(argv[1]),std::filesystem::u8path(argv[2]));
        return mode=="fail"?1:0;
    }
    if(argc==6&&std::string(argv[1])=="--compose-outline"){
        try{gfx::WriteBmp(std::filesystem::u8path(argv[5]),gfx::ComposeDrawOutline(gfx::LoadImage(std::filesystem::u8path(argv[2])),gfx::LoadImage(std::filesystem::u8path(argv[3])),gfx::LoadImage(std::filesystem::u8path(argv[4]))));return 0;}
        catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    }
    if (argc > 1 && (std::string(argv[1]) == "--screenshots" || std::string(argv[1]) == "--quit-after-frame") && std::getenv("GFXRECON_GUI_TEST_SLOW_REPLAY")) {
        std::this_thread::sleep_for(std::chrono::seconds(8)); return 1;
    }
    if(argc==9&&std::string(argv[1])=="--inspect-draw"){
        try{
            const auto capture=gfx::ReadCapture(std::filesystem::u8path(argv[3]));const auto block=std::stoull(argv[7]);size_t selected=0;bool found=false;
            for(size_t i=0;i<capture->executions.size();++i)if(capture->events[capture->executions[i].event].block==block){selected=i;found=true;break;}if(!found)throw std::runtime_error("Draw unavailable");
            std::ifstream config(std::filesystem::u8path(argv[6]));const auto tools=gfx::Json::parse(config).get<std::map<std::string,std::string>>();
            gfx::ReplayRequest request;request.capture=std::filesystem::u8path(argv[2]);request.output=std::filesystem::u8path(argv[5]);request.cacheDirectory=request.output.parent_path()/"probe-cache";
            request.frame=capture->executions[selected].frame;request.dump=capture->DumpRequest(selected);request.outputsOnly=true;request.reuseArtifacts=false;request.timeout=std::chrono::seconds(30);
            std::string variant=argv[8];if(variant.rfind("validate:",0)==0){request.validation=true;variant.erase(0,9);}request.drawInspection=variant=="full";request.descriptorImages=variant=="textures";
            if(variant=="solid"||variant=="wireframe"||variant=="depth"||variant=="silhouette"||variant=="uv")request.visualization=variant;
            if(variant.rfind("subdraw",0)==0){const auto colon=variant.find(':');request.indirectSubdraw=std::stoi(variant.substr(7,colon==std::string::npos?colon:colon-7));if(colon!=std::string::npos)request.visualization=variant.substr(colon+1);}
            request.visualizationPipeline=capture->events[capture->executions[selected].event].pipeline;
            if(variant=="uv"){
                const auto shaders=capture->jsonl.parent_path()/"shaders";
                const auto work=request.output.parent_path()/(request.output.filename().u8string()+"-shader");std::filesystem::create_directories(work);
                const auto& stages=capture->pipelines.at(request.visualizationPipeline);const auto stage=std::find_if(stages.begin(),stages.end(),[](const auto& s){return s.stage.find("FRAGMENT")!=std::string::npos;});
                if(stage==stages.end())throw std::runtime_error("Missing fragment stage");std::string detail;
                const auto source=work/"checker.frag";{std::ofstream out(source);out<<gfx::UvCheckerboardSource(shaders/("sh"+std::to_string(stage->module)),stage->entry,detail);}
                request.uvShader=work/"checker.spv";gfx::CompileShader(source,{0,"VK_SHADER_STAGE_FRAGMENT_BIT","main"},request.uvShader,tools);std::cout<<detail<<'\n';
            }
            if(request.indirectSubdraw>=0){for(const auto& stage:capture->pipelines.at(request.visualizationPipeline))if(gfx::ShaderUsesDrawIndex(capture->jsonl.parent_path()/"shaders"/("sh"+std::to_string(stage.module))))throw std::runtime_error("gl_DrawID not supported");request.indirectDrawIdSafe=true;}
            if(request.visualization=="silhouette"){
                auto context=request;context.visualization="original";context.output=request.output.parent_path()/(request.output.filename().u8string()+"-original");
                const gfx::ReplayTarget target{std::string(argv[4])!="local",std::string(argv[4])=="local"?"":argv[4]};
                auto color=[](const auto& assets){for(const auto& asset:assets)if(asset.metadata.value("category","")=="output"&&asset.metadata.value("aspect","")=="COLOR"&&asset.metadata.value("location",0u)==0)return asset.file;throw std::runtime_error("No COLOR 0 for outline probe");};
                request.outlineBaseImage=color(gfx::ReplayCapture(context,target,tools).resources);
                context.visualization="solid";context.output=request.output.parent_path()/(request.output.filename().u8string()+"-solid");
                request.outlineCoverageImage=color(gfx::ReplayCapture(context,target,tools).resources);
            }
            const auto result=gfx::ReplayCapture(request,{std::string(argv[4])!="local",std::string(argv[4])=="local"?"":argv[4]},tools);std::cout<<result.timings.dump(2)<<"\nResources: "<<result.resources.size()<<'\n';return 0;
        }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    }
    if(argc==7&&std::string(argv[1])=="--screen-off-replay"){
        try{
            const auto source=std::filesystem::u8path(argv[2]),jsonl=std::filesystem::u8path(argv[3]),root=std::filesystem::u8path(argv[5]);
            std::ifstream toolFile(std::filesystem::u8path(argv[6]));const auto tools=gfx::Json::parse(toolFile).get<std::map<std::string,std::string>>();const auto capture=gfx::ReadCapture(jsonl);
            gfx::AdbConnection adb{tools.at("adb"),argv[4]};gfx::EnsureAndroidReplayReady(adb);const auto initial=gfx::ReadAndroidPanelState(adb);
            const auto hash=gfx::Sha256File(source);gfx::Json report=gfx::Json::array();std::map<std::string,std::string> normal;
            for(bool draw:{false,true})for(bool off:{false,true}){
                gfx::ReplayRequest request;request.capture=source;request.output=root/(std::string(draw?"draw":"frame")+(off?"-off":"-on"));request.cacheDirectory=root/"cache";
                request.frame=1;request.screenOff=off;request.reuseArtifacts=false;request.expectedCaptureHash=hash;
                if(draw){size_t selected=0;bool found=false;for(size_t i=0;i<capture->executions.size();++i)if(capture->executions[i].frame==1&&capture->events[capture->executions[i].event].name.rfind("vkCmdDraw",0)==0){selected=i;found=true;break;}if(!found)throw std::runtime_error("No graphics draw in the requested frame");request.dump=capture->DumpRequest(selected);request.outputsOnly=true;request.drawInspection=true;}
                const auto result=gfx::ReplayCapture(request,{true,argv[4]},tools);
                std::map<std::string,std::string> images;
                if(!draw)images["frame"]=gfx::Sha256File(result.image);
                else for(const auto& asset:result.resources)if(asset.file.extension()==".bmp")images[asset.metadata.dump()]=gfx::Sha256File(asset.file);
                if(images.empty())throw std::runtime_error("Replay returned no images");
                if(!off)normal=images;else{
                    if(images!=normal)throw std::runtime_error("Screen-off pixels differ from normal replay");
                    std::ifstream file(request.output/"display-journal.json");const auto journal=gfx::Json::parse(file);
                    if(!journal.value("restored",false)||journal.value("off_observations",0)<2)throw std::runtime_error("Missing physical display-off/restoration proof");
                }
                if(gfx::ReadAndroidPanelState(adb).off!=initial.off)throw std::runtime_error("Replay changed the initial physical panel state");
                report.push_back({{"draw",draw},{"screen_off",off},{"images",images.size()},{"timing",result.timings}});gfx::AtomicJson(root/"result.json",report);
                std::cout<<request.output.filename().u8string()<<": "<<images.size()<<" images, "<<result.timings.value("elapsed_seconds",0.0)<<" s\n"<<std::flush;
            }
            if(gfx::Sha256File(source)!=hash)throw std::runtime_error("Original capture was modified");return 0;
        }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    }
    if(argc==6&&std::string(argv[1])=="--validate-replay") {
        try{
            std::ifstream input(std::filesystem::u8path(argv[5]));const auto tools=gfx::Json::parse(input).get<std::map<std::string,std::string>>();
            gfx::ReplayRequest request;request.capture=std::filesystem::u8path(argv[2]);request.output=std::filesystem::u8path(argv[4]);request.cacheDirectory=request.output.parent_path()/"validation-cache";request.validation=true;request.timeout=std::chrono::seconds(600);
            const auto result=gfx::ReplayCapture(request,{std::string(argv[3])!="local",std::string(argv[3])=="local"?"":argv[3]},tools);
            std::cout<<result.validationReport.dump(2)<<'\n';return 0;
        }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    }
    if(argc==7&&std::string(argv[1])=="--inspect-mesh") {
        try {
            const auto capture=gfx::ReadCapture(std::filesystem::u8path(argv[2]));const auto block=std::stoull(argv[5]);
            for(size_t i=0;i<capture->executions.size();++i)if(capture->events[capture->executions[i].event].block==block){
                const auto& stage=capture->pipelines.at(capture->events[capture->executions[i].event].pipeline).front();
                const auto data=gfx::ReadMeshData(*capture,i,std::filesystem::u8path(argv[3]),std::filesystem::u8path(argv[4])/("sh"+std::to_string(stage.module)));
                gfx::Json result{{"block",block},{"vertices",data.vertexIds.size()},{"diagnostic",data.diagnostic},{"post_diagnostic",data.postDiagnostic},{"post_source",!data.vertexSource.empty()},{"attributes",gfx::Json::array()}};
                for(const auto& a:data.attributes)result["attributes"].push_back({{"location",a.location},{"count",a.values.size()},{"first",a.values.front()}});
                std::ofstream out(std::filesystem::u8path(argv[6]));out<<result.dump(2);std::cout<<result.dump(2)<<'\n';return 0;
            }throw std::runtime_error("Draw not found");
        }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    }
    if (argc == 7 && (std::string(argv[1]) == "--profile-replay" || std::string(argv[1]) == "--profile-replay-auto")) {
        try { gfx::tests::ProfileReplay(std::filesystem::u8path(argv[2]),std::filesystem::u8path(argv[3]),argv[4],std::filesystem::u8path(argv[5]),std::filesystem::u8path(argv[6]),std::string(argv[1])=="--profile-replay-auto"); return 0; }
        catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    }
    if (argc == 9 && std::string(argv[1]) == "--dump-feasibility") {
        try { gfx::tests::DumpFeasibility(std::filesystem::u8path(argv[2]),std::filesystem::u8path(argv[3]),argv[4],std::filesystem::u8path(argv[5]),std::filesystem::u8path(argv[6]),static_cast<uint32_t>(std::stoul(argv[7])),argv[8]); return 0; }
        catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    }
    if (argc == 7 && std::string(argv[1]) == "--live-capture") {
        try { gfx::tests::RunLiveCaptureTests(argv[2],argv[3],argv[4],argv[5],std::filesystem::u8path(argv[6])); return 0; }
        catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    }
    if(argc==5&&std::string(argv[1])=="--describe-interface"){
        try{std::ofstream out(std::filesystem::u8path(argv[4]));out<<gfx::DescribeShaderInterface(std::filesystem::u8path(argv[2]),argv[3]);return 0;}catch(const std::exception& e){std::cerr<<e.what();return 1;}
    }
    if (argc == 5 && std::string(argv[1]) == "--interface") {
        try {
            gfx::CheckShaderInterface(std::filesystem::u8path(argv[2]), std::filesystem::u8path(argv[3]), argv[4]);
            std::cout << "Interfaces compatible\n"; return 0;
        } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    }
    if (argc == 4 && std::string(argv[1]) == "--shader-metadata") {
        try {
            const auto metadata = gfx::ReadShaderMetadata(gfx::ReadSpirvBinary(std::filesystem::u8path(argv[2])));
            std::ofstream out(std::filesystem::u8path(argv[3])); out << metadata.debug.dump(2);
            return out ? 0 : 1;
        } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    }
    if (argc == 5 && std::string(argv[1]) == "--dump-request") {
        try {
            const auto capture = gfx::ReadCapture(std::filesystem::u8path(argv[2]));
            const auto request = capture->DumpRequest(std::stoull(argv[3]));
            std::ofstream out(std::filesystem::u8path(argv[4])); out << request.dump(2);
            return out ? 0 : 1;
        } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    }
    if (argc == 3 && std::string(argv[1]) == "--benchmark-index") {
        try {
            const auto directory = std::filesystem::u8path(argv[2]); std::filesystem::create_directories(directory);
            gfx::tests::BenchmarkIndex(directory); return 0;
        } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    }
    if (argc >= 3 && std::string(argv[1]) == "--run") {
        gfx::ProcessRequest request{argv[2], std::vector<std::string>(argv + 3, argv + argc)};
        request.timeout = std::chrono::seconds(15);
        try {
            const auto result = gfx::RunProcess(request);
            std::cout << result.output << "\nexit=" << result.exitCode << " timeout=" << result.timedOut << '\n';
            return result.Success() ? 0 : 1;
        } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    }
    if (argc > 1 && std::string(argv[1]) == "--echo") {
        const auto env = std::getenv("GFX_GUI_TEST_VALUE");
        std::string value = env ? env : "";
#ifdef _WIN32
        const auto wideEnv = _wgetenv(L"GFX_GUI_TEST_VALUE");
        if (wideEnv) {
            int n = WideCharToMultiByte(CP_UTF8, 0, wideEnv, -1, nullptr, 0, nullptr, nullptr);
            value.resize(n);
            WideCharToMultiByte(CP_UTF8, 0, wideEnv, -1, value.data(), n, nullptr, nullptr);
            value.pop_back();
        }
#endif
        std::cout << gfx::Json{{"args", std::vector<std::string>(argv + 2, argv + argc)}, {"env", value}}.dump() << std::flush;
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--sleep") { std::this_thread::sleep_for(std::chrono::seconds(10)); return 0; }
    if (argc > 1 && std::string(argv[1]) == "--copy-stream") {
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);
#endif
        char buffer[8192];
        while (std::cin.read(buffer, sizeof(buffer)) || std::cin.gcount()) std::cout.write(buffer, std::cin.gcount());
        std::cerr << "copy-ok"; return 0;
    }
    const auto dir = std::filesystem::temp_directory_path() / ("gfx-gui-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(dir);
        const auto validation=gfx::ParseValidationLog("[gfxrecon] INFO - Replay has added the following required layers to VkInstanceCreateInfo:\n[gfxrecon] INFO - \tVK_LAYER_KHRONOS_validation\n[gfxrecon] ERROR - VUID-VkGraphicsPipelineCreateInfo-test-00001 wrong pipeline\n[gfxrecon] WARNING - VUID-vkCmdDraw-test-00002 warning\n");
        CHECK(validation["enabled"]==true&&validation["errors"]==1&&validation["warnings"]==1);
        CHECK(!gfx::ParseValidationLog("Replay has removed layers:\n\tVK_LAYER_KHRONOS_validation")["enabled"].get<bool>());
        CHECK(!gfx::ParseValidationLog("Failed to enable validation layer VK_LAYER_KHRONOS_validation")["enabled"].get<bool>());
        for(unsigned turn=0;turn<4;++turn)for(int flips=0;flips<4;++flips){
            gfx::ImageOrientation view;view.turns=turn;view.horizontal=flips&1;view.vertical=flips&2;
            for(int y=0;y<view.Height(2,3);++y)for(int x=0;x<view.Width(2,3);++x){
                const auto pixel=view.SourcePixel(x,y,2,3);const auto uv=view.Uv((x+.5f)/view.Width(2,3),(y+.5f)/view.Height(2,3));
                CHECK(pixel[0]==static_cast<int>(uv[0]*2)&&pixel[1]==static_cast<int>(uv[1]*3));
            }
        }
        gfx::ImageOrientation clockwise;clockwise.Rotate();CHECK((clockwise.SourcePixel(0,0,2,3)==std::array<int,2>({0,2})));
        clockwise.Rotate();clockwise.Rotate();clockwise.Rotate();CHECK(clockwise.turns==0);
        gfx::ImageDisplayRange range;CHECK(range.Map(123)==123);range.maximum=2.0f/255;CHECK(range.Map(1)==128);CHECK(range.Map(3)==255);
        range.minimum=1.0f/255;CHECK(range.Map(1)==0);CHECK(range.Map(2)==255);range={.00001f,.00003f};CHECK(range.MapValue(.00002f)==128);
        uint32_t packed=0xff123456u;const auto depth=gfx::DecodeDepthSample(reinterpret_cast<uint8_t*>(&packed),"VK_FORMAT_D24_UNORM_S8_UINT");CHECK(std::abs(depth-0x123456/16777215.0f)<1e-7);
        const auto plan=gfx::PlanImageReadback(false,11,16,0,1,0,1);CHECK(plan.levels==1&&plan.layers==1&&plan.Index(0,0)==0);
        const auto offset=gfx::PlanImageReadback(false,11,16,3,2,7,2);CHECK(offset.levels==5&&offset.layers==2&&offset.baseLayer==7&&offset.Index(3,8)==7);
        CHECK(gfx::PlanImageReadback(false,11,16,20,1,0,1).levels==0);
        const auto details=gfx::ImageDetails({{"imageId",7},{"format","VK_FORMAT_D24_UNORM_S8_UINT"},{"aspect","DEPTH"},{"dimensions",{64,32,1}},{"mipLevel",2},{"drawIndex",20}});
        CHECK(details.find("64 x 32")!=std::string::npos&&details.find("Mip 2")!=std::string::npos&&details.find("drawIndex")==std::string::npos&&details.find('{')==std::string::npos);
        clockwise.Rotate(-1);CHECK(clockwise.turns==3);clockwise.Rotate();CHECK(clockwise.turns==0);
        CHECK(gfx::DescribeAndroidReplayExit("ApplicationExitInfo #0: reason=3 (LOW_MEMORY)").find("memory")!=std::string::npos);
        CHECK(gfx::DescribeAndroidReplayExit("ApplicationExitInfo #0: reason=2 (SIGNALED) ApplicationExitInfo #1: reason=3 (LOW_MEMORY)").find("memory")==std::string::npos);
        SyntaxTests(); MetadataTests(dir); CaptureTests(dir); ReplayTargetTests(); CaptureControlTests(); ProcessTests(std::filesystem::absolute(argv[0]).u8string());
        BinaryTransferTest(std::filesystem::absolute(argv[0]).u8string(), dir);
        ResourceTests(dir); ReplayIdentityTests(dir);
        gfx::tests::RunAnalysisTests(dir);
        gfx::tests::RunCacheTests(dir);
        gfx::tests::RunPipelineCacheTests(dir);
        gfx::tests::RunOptimizerTests(dir);
        gfx::tests::RunCaptureSaveTests(dir);
        gfx::tests::RunDeviceTests(dir);
        gfx::tests::RunAndroidCaptureTests(dir);
        gfx::tests::RunMeshTests(dir);
        gfx::tests::RunExportTests(dir);
        gfx::tests::RunImageDecodeTests(dir);
        gfx::tests::RunCliTests(dir);
        gfx::tests::RunReplayCliTests(dir);
        std::filesystem::remove(dir / "fixture.jsonl"); std::filesystem::remove(dir);
        std::cout << "Core checks passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
