#include "analysis/relations.h"
#include "analysis/export.h"
#include "core/artifacts.h"

#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace gfx::tests {
namespace {
namespace fs=std::filesystem;
constexpr uint64_t Pipeline=uint64_t(std::numeric_limits<int64_t>::max())+17;
constexpr uint64_t Module=std::numeric_limits<uint64_t>::max();
void Require(bool good,const std::string& text){if(!good)throw std::runtime_error("Relation query: "+text);}
template<class F>void Fails(F action,const char* code){try{action();}catch(const RelationError& error){Require(error.code==code,std::string("Expected ")+code+", got "+error.code+": "+error.what());return;}throw std::runtime_error(std::string("Relation query unexpectedly accepted ")+code);}
Capture Fixture(const fs::path& root){
    Capture capture;capture.source=root/"source.gfxr";capture.jsonl=root/"source.jsonl";capture.frames=2;
    {std::ofstream source(capture.source);source<<"Synthetic relation query fixture";}capture.fingerprint=Sha256File(capture.source);
    std::ofstream output(capture.jsonl,std::ios::binary);
    auto event=[&](uint64_t block,const char* command,Json args,uint64_t pipeline){
        Event e;e.block=block;e.name=command;e.pipeline=pipeline;e.offset=uint64_t(output.tellp());e.commandBuffer=100;e.draw=true;
        e.state=std::make_shared<const Json>(Json{{"dynamic",{{"vkCmdSetCullMode",{{"cullMode","VK_CULL_MODE_NONE"}}}}},
            {"vertexBuffers",{{"0",{{"buffer",71},{"offset",0}}}}}});
        output<<Json{{"index",block},{"function",{{"name",command},{"args",args}}}}.dump()<<'\n';capture.events.push_back(e);
    };
    event(10,"vkCmdBeginRenderPass",Json::object(),0);
    event(20,"vkCmdDraw",{{"vertexCount",Module},{"instanceCount",0}},Pipeline);
    event(30,"vkCmdDrawIndexedIndirectCount",{{"buffer",71},{"drawCount",40},{"maxDrawCount",99},{"countBuffer",73}},Pipeline);
    event(40,"vkCmdDrawIndexed",{{"indexCount",0},{"instanceCount",2}},7);
    event(50,"vkCmdDispatch",{{"groupCountX",8},{"groupCountY",4},{"groupCountZ",1}},101);
    event(Module,"vkCmdDraw",{{"vertexCount",3},{"instanceCount",1}},0);output.close();
    capture.pipelineInfo[Pipeline]={{"pRasterizationState",{{"cullMode","VK_CULL_MODE_BACK_BIT"}}},{"pDynamicState",{{"pDynamicStates",{"VK_DYNAMIC_STATE_CULL_MODE"}}}}};
    capture.pipelineInfo[7]=capture.pipelineInfo[Pipeline];capture.pipelineInfo[99]=nullptr;
    capture.pipelineInfo[101]={{"stage",{{"stage","VK_SHADER_STAGE_COMPUTE_BIT"}}}};
    capture.pipelines[Pipeline]={{Module,"VK_SHADER_STAGE_VERTEX_BIT","vertexMain",{{"identity","SPECIALIZATION_DETAIL_ONLY"}}},{43,"VK_SHADER_STAGE_FRAGMENT_BIT","fragmentMain",nullptr}};
    capture.pipelines[7]={{Module,"VK_SHADER_STAGE_VERTEX_BIT","otherVertex",nullptr},{Module,"VK_SHADER_STAGE_FRAGMENT_BIT","otherFragment",nullptr}};
    capture.pipelines[101]={{0,"VK_SHADER_STAGE_COMPUTE_BIT","main",nullptr}};
    capture.objects[Module]={"ShaderModule","maximum module",2,0,Json::object()};capture.objects[43]={"ShaderModule","fragment module",3,0,Json::object()};
    capture.objects[Pipeline]={"Pipeline","large pipeline",4,0,capture.pipelineInfo[Pipeline]};capture.objects[71]={"Buffer","vertex resource",5,0,Json::object()};
    auto execute=[&](size_t index,uint32_t frame,uint32_t slot){
        Execution x;x.event=index;x.frame=frame;x.submit=frame==1?1000:1001;x.begin=9;x.generation=2;x.primary=slot==0;
        x.invocationPath={100,200};x.invocations={{100,9,2,0,slot,{},nullptr},{200,8,4,19,slot,{},nullptr}};
        if(index!=4)x.renderScope={10,60};x.descriptorValues={{"secret","DESCRIPTOR_BYTES_MUST_NOT_APPEAR"}};capture.executions.push_back(x);
    };
    execute(1,1,0);execute(1,1,1);execute(1,2,0);execute(2,2,0);execute(3,2,0);execute(4,2,0);execute(5,2,0);
    capture.executions.back().valid=false;capture.executions.back().unavailable="Unresolved secondary recording";
    return capture;
}
}
void RelationQueryTests(const fs::path& root){
    const auto work=root/"relation-query";fs::create_directories(work);auto capture=Fixture(work);
    const auto index=BuildRelationSnapshot(capture,work/"snapshot.sqlite");
    RelationQuery query;query.includeCount=true;
    auto result=QueryRelations(index,query);
    Require(result.rows.size()==7&&result.total==7,"Submitted repetitions or full matching count were lost");
    Require(result.rows[0]["execution"]==0&&result.rows[1]["execution"]==1&&result.rows[0]["block"]==result.rows[1]["block"],"Execution identity deduplicated API blocks");
    Require(result.rows[0]["vertex_count"]==Module&&result.rows[0]["instance_count"]==0,"Unsigned or valid zero counts changed");
    Require(result.rows[3]["vertex_count"].is_null()&&result.rows[3]["index_count"].is_null()&&result.rows[3]["instance_count"].is_null()&&!result.rows[3]["unavailable"].empty(),"Indirect maxima were inferred as actual counts");
    const auto compact=result.ToJson().dump();Require(compact.find("SPECIALIZATION_DETAIL_ONLY")==std::string::npos&&compact.find("DESCRIPTOR_BYTES")==std::string::npos,"Default rows expanded detailed state");
    Require(result.coverage["unresolved_candidate_executions"].get<uint64_t>()>=2,"Unresolved candidate coverage missing");

    query={};query.filters.shaders={43};query.filters.stages={"vertex"};result=QueryRelations(index,query);
    Require(result.rows.empty()&&!result.coverage["negative_answer_complete"].get<bool>(),"Shader and stage were matched on unrelated edges or empty match overstated completeness");
    query.filters.shaders={Module};query.filters.stages={"frag"};result=QueryRelations(index,query);
    Require(result.rows.size()==1&&result.rows[0]["execution"]==4,"Fragment filter crossed vertex-only module edge");
    query.filters.stages.clear();result=QueryRelations(index,query);Require(result.rows.size()==5,"Multiple shader edges multiplied one execution");
    query.filters.frames={2};query.filters.pipelines={Pipeline,7};query.filters.shaders={Module,43};query.filters.stages={"fragment"};
    result=QueryRelations(index,query);Require(result.rows.size()==3,"Same-field OR / cross-field AND semantics changed");

    query={};query.limit=2;query.columns={"execution","block"};
    std::set<uint64_t> pages;
    do{
        result=QueryRelations(index,query);Require(!result.total,"Unrequested total count present");
        for(const auto& row:result.rows)Require(pages.insert(row.at("execution").get<uint64_t>()).second,"Cursor repeated an execution");
        query.cursor=result.nextCursor;
    }while(result.hasMore);
    Require(pages.size()==7,"Keyset pages omitted executions");
    query.cursor.clear();result=QueryRelations(index,query);const auto cursor=result.nextCursor;
    query.cursor=cursor;query.columns={"block","execution"};Fails([&]{QueryRelations(index,query);},"cursor_mismatch");
    query.columns={"execution","block"};query.filters.frames={2};Fails([&]{QueryRelations(index,query);},"cursor_mismatch");
    query.filters.frames.clear();query.cursor="not-a-valid-cursor";Fails([&]{ValidateRelationQuery(query);},"invalid_cursor");
    query.cursor=cursor;query.limit=3;query.includeCount=true;result=QueryRelations(index,query);Require(result.rows.size()==3&&result.total==7,"Page size/count option unnecessarily invalidated cursor");
    query={};query.filters.shaders={Module};query.filters.stages={"vertex"};query.limit=1;result=QueryRelations(index,query);
    query.cursor=result.nextCursor;query.filters.stages={"VK_SHADER_STAGE_VERTEX_BIT","vert"};Require(QueryRelations(index,query).rows.size()==1,"Normalized stage aliases invalidated the same query cursor");

    query={};query.entity=RelationEntity::Pipelines;query.filters.shaders={Module};query.filters.stages={"fragment"};
    result=QueryRelations(index,query);Require(result.rows.size()==1&&result.rows[0]["pipeline_id"]==7&&result.rows[0]["execution_count"]==1,"Pipeline stage/use counts are not scoped consistently");
    query.filters.pipelines={99};result=QueryRelations(index,query);Require(result.rows.empty()&&!result.coverage["negative_answer_complete"].get<bool>(),"Unused unresolved pipeline looked like complete negative shader evidence");
    query={};query.entity=RelationEntity::Pipelines;result=QueryRelations(index,query);Require(result.rows.front()["pipeline_id"]==7&&result.rows.back()["pipeline_id"]==Pipeline,"Pipeline ordering crossed INT64 boundary");
    query={};query.entity=RelationEntity::Shaders;query.filters.frames={2};query.filters.shaders={Module};query.filters.stages={"fragment"};query.columns={"shader_id","execution_count","pipeline_count","stages"};
    result=QueryRelations(index,query);Require(result.rows.size()==1&&result.rows[0]["execution_count"]==1&&result.rows[0]["pipeline_count"]==1&&result.rows[0]["stages"]==Json::array({"fragment"}),"Shader stage-scoped summary counted unrelated stage uses");
    query={};query.entity=RelationEntity::Passes;query.filters.frames={1};query.filters.passes={0};query.filters.shaders={Module};
    result=QueryRelations(index,query);Require(result.rows.size()==1&&result.rows[0]["execution_count"]==1,"Pass membership merged distinct submission slots");
    query.filters.passes.clear();result=QueryRelations(index,query);uint64_t framePassExecutions=0;
    for(const auto& row:result.rows)framePassExecutions+=row.at("execution_count").get<uint64_t>();
    Require(result.rows.size()==2&&framePassExecutions==2,"Pass summaries lost repeated invocations");

    query={};query.groupBy={"shader_id"};query.includeCount=true;result=QueryRelations(index,query);
    Require(result.rows.size()==2&&result.total==2&&result.coverage["shader_groups_overlap"]==true,"Shader grouped-count identity missing");
    uint64_t overlapping=0;for(const auto& row:result.rows)overlapping+=row.at("execution_count").get<uint64_t>();Require(overlapping==9,"Shader groups were summed/deduplicated incorrectly");
    query.filters.shaders={Module};query.filters.stages={"fragment"};result=QueryRelations(index,query);Require(result.rows.size()==1&&result.rows[0]["execution_count"]==1,"Shader grouping ignored same-edge filtering");
    query={};query.groupBy={"pipeline_id"};query.limit=1;result=QueryRelations(index,query);Require(result.rows.size()==1&&result.rows[0]["pipeline_id"].is_null()&&result.hasMore,"Nullable pipeline group cursor setup");
    query.cursor=result.nextCursor;result=QueryRelations(index,query);Require(!result.rows[0]["pipeline_id"].is_null(),"Null pipeline group cursor did not advance");
    query={};query.groupBy={"pass"};result=QueryRelations(index,query);uint64_t total=0;
    for(const auto& row:result.rows){Require(row.contains("frame")&&row.contains("pass"),"Pass grouping omitted frame identity");total+=row.at("execution_count").get<uint64_t>();}Require(total==7,"Pass groups multiplied executions");

    query={};query.filters.executions={0};query.columns={"execution"};query.sections={"rasterization","resource_refs","provenance","shaders"};result=QueryRelations(index,query);
    const auto& detail=result.rows[0];Require(detail["rasterization"]["static"]["cullMode"]=="VK_CULL_MODE_BACK_BIT"&&detail["rasterization"]["dynamic_recorded"]["vkCmdSetCullMode"]["cullMode"]=="VK_CULL_MODE_NONE"&&detail["rasterization"]["effective_status"]=="unresolved","Rasterization guessed effective state or dropped dynamic evidence");
    Require(detail["shaders"][0]["entry_point"]=="vertexMain"&&detail["shaders"][0]["specialization"]["identity"]=="SPECIALIZATION_DETAIL_ONLY","Explicit shader section lost entry/specialization");
    Require(detail["provenance"]["dump_availability"]=="not_generated"&&detail.dump().find("DESCRIPTOR_BYTES")==std::string::npos,"Evidence/resource section materialized bytes or falsely claimed a dump");
    fs::remove(capture.source);fs::remove(capture.jsonl);result=QueryRelations(index,query);Require(result.rows.size()==1&&result.source["evidence_availability"]["source_file"]=="missing_or_unavailable","Explicit snapshot tried to reopen original capture or lied about evidence");

    query={};query.columns={"descriptor_bytes"};Fails([&]{ValidateRelationQuery(query);},"unknown_field");
    query={};query.sections={"shader_text"};Fails([&]{ValidateRelationQuery(query);},"unknown_section");
    query={};query.filters.passes={0};query.filters.frames={1,2};Fails([&]{ValidateRelationQuery(query);},"ambiguous_pass");
    query={};query.limit=1001;Fails([&]{ValidateRelationQuery(query);},"invalid_limit");
    query={};query.groupBy={"pass","shader_id"};Fails([&]{ValidateRelationQuery(query);},"invalid_group");
    query={};std::atomic_bool cancel{true};Fails([&]{QueryRelations(index,query,&cancel);},"cancelled");
    auto stale=index;stale.snapshot="different-generation";Fails([&]{QueryRelations(stale,query);},"snapshot_changed");
    Require(NormalizeRelationStage("VK_SHADER_STAGE_FRAGMENT_BIT")=="fragment"&&NormalizeRelationStage("tesc")=="tess-control","Stage alias catalog");
    Fails([]{NormalizeRelationStage("SQL OR 1=1");},"invalid_stage");
    const auto catalog=RelationFieldCatalog(RelationEntity::Executions);Require(catalog.at("schema")==kRelationQuerySchema&&catalog.at("page")["maximum_limit"]==1000,"Field catalog schema/page contract");
    Require(RelationFieldCatalog(RelationEntity::Pipelines).at("group_by").empty(),"Catalog advertised unsupported pipeline grouping");
    Require(catalog.at("aggregate_fields").size()==4&&catalog.at("aggregate_fields")[0].at("name")=="execution_count","Catalog omitted aggregate field discovery");
    fs::remove_all(work);
}
}
