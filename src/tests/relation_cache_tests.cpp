#include "analysis/relations.h"
#include "analysis/relations_internal.h"
#include <fstream>
#include <future>
#include <thread>

namespace gfx::tests {
namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(std::string("Relation cache: ") + message); }
template<class F> void Reject(F action, const std::string& code) {
    try { action(); } catch (const RelationError& error) { Require(error.code == code, error.what()); return; }
    throw std::runtime_error("Relation cache: expected " + code);
}
}
void RelationCacheTests(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    const auto directory = root / "relation-cache"; fs::create_directories(directory);
    const auto source = directory / "source.jsonl", cache = directory / "cache";
    const auto call = [](int n, const char* name, Json args) { return Json{{"index",n},{"function",{{"name",name},{"args",args}}}}; };
    std::string original;
    for (const auto& row : {call(1,"vkBeginCommandBuffer",{{"commandBuffer",10}}),
        call(2,"vkCmdDraw",{{"commandBuffer",10},{"vertexCount",3},{"instanceCount",1}}),
        call(3,"vkEndCommandBuffer",{{"commandBuffer",10}}),
        call(4,"vkQueueSubmit",{{"pSubmits",{{{"pCommandBuffers",{10}}}}}}),
        call(5,"vkQueuePresentKHR",Json::object())}) original += row.dump() + "\n";
    { std::ofstream out(source); out << original; }
    const auto first = EnsureRelationIndex(source, cache, {});
    Require(!first.reused && fs::is_regular_file(first.path), "First generation was not built");
    const auto reused = EnsureRelationIndex(source, cache, {});
    Require(reused.reused && reused.snapshot == first.snapshot, "Valid generation was not reused");
    RelationQuery query; query.columns = {"execution","block"};
    Require(QueryRelations(first, query).rows.size() == 1, "Initial snapshot query");
    RelationBuildOptions rebuild; rebuild.rebuild = true;
    const auto second = EnsureRelationIndex(source, cache, {}, rebuild);
    Require(second.snapshot != first.snapshot && fs::is_regular_file(first.path), "Replacement destroyed its predecessor");
    Require(QueryRelations(first, query).rows.size() == 1, "Old snapshot stopped working after publication");
    std::atomic_bool cancelled{true};
    Reject([&] { EnsureRelationIndex(source, cache, {}, rebuild, &cancelled); }, "cancelled");
    Require(EnsureRelationIndex(source, cache, {}).snapshot == second.snapshot, "Cancellation replaced the current generation");
    std::atomic_bool entered{false}, release{false};
    rebuild.progress = [&](const std::string& message) {
        if (message == "Preparing static capture relations") {
            entered = true; while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    auto replacing = std::async(std::launch::async, [&] { return EnsureRelationIndex(source, cache, {}, rebuild); });
    try {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!entered && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Require(entered, "Concurrent build did not reach its private generation");
        Require(QueryRelations(second, query).rows.size() == 1, "Reader blocked or changed during replacement");
        release = true;
    } catch (...) { release = true; throw; }
    const auto third = replacing.get();
    Require(third.snapshot != second.snapshot && QueryRelations(second, query).rows.size() == 1, "Concurrent publication changed an existing reader");
    rebuild.progress = [&](const std::string& message) {
        if (message == "Preparing static capture relations") { std::ofstream out(source, std::ios::app); out << "\n"; }
    };
    bool changed = false;
    try { EnsureRelationIndex(source, cache, {}, rebuild); }
    catch (const RelationError& error) { changed = error.code.find("source") != std::string::npos; }
    Require(changed, "Source mutation was published");
    { std::ofstream out(source, std::ios::trunc); out << original; }
    Require(EnsureRelationIndex(source, cache, {}).snapshot == third.snapshot, "Failed build replaced current generation");
    { relation_detail::Database db(third.path, true); db.Exec("PRAGMA user_version=99"); }
    Reject([&] { EnsureRelationIndex(source, cache, {}); }, "relation_index_version");
    Reject([&] { EnsureRelationIndex(source, cache, {}, RelationBuildOptions{true,{}}); }, "relation_index_version");
    { relation_detail::Database db(third.path, true); db.Exec("PRAGMA user_version=1"); }
    const auto repaired = EnsureRelationIndex(source, cache, {});
    Require(repaired.snapshot != third.snapshot, "Changed database digest was silently reused");
    { std::fstream corrupt(repaired.path, std::ios::in | std::ios::out | std::ios::binary); corrupt << "NOT SQLITE"; }
    const auto recovered = EnsureRelationIndex(source, cache, {});
    Require(recovered.snapshot != repaired.snapshot && QueryRelations(recovered, query).rows.size() == 1, "Owned corrupt generation was not rebuilt");
    const auto manifestPath = recovered.path.parent_path().parent_path() / "current.json";
    Json manifest; { std::ifstream in(manifestPath); in >> manifest; }
    for (const char* key : {"schema", "builder", "parser_schema", "grouping_version"}) {
        auto future = manifest; future[key] = 999;
        { std::ofstream out(manifestPath); out << future.dump(); }
        Reject([&] { EnsureRelationIndex(source, cache, {}); }, "relation_index_version");
        Reject([&] { EnsureRelationIndex(source, cache, {}, RelationBuildOptions{true,{}}); }, "relation_index_version");
        Require(QueryRelations(recovered, query).rows.size() == 1, "Future manifest changed its prior snapshot");
    }
    { std::ofstream out(manifestPath); out << manifest.dump(); }
    Require(EnsureRelationIndex(source, cache, {}).snapshot == recovered.snapshot, "Restored manifest was not reused");
    const auto raw = directory / "source.gfxr"; { std::ofstream out(raw); out << "not decoded"; }
    Reject([&] { EnsureRelationIndex(raw, cache, {}); }, "missing_tool");
    fs::remove_all(directory);
}
} // namespace gfx::tests
