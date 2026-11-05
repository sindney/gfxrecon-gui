#pragma once

#include "generated/generated_vulkan_dispatch_table.h"
#include "gui_gpu_profiling_sha256.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <tuple>
#if defined(__linux__)
#include <time.h>
#endif

namespace gfx_gui_gpu {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
inline Json request, report;
inline std::string output_file;
inline bool enabled=false;
inline uint64_t timeout_ns=30'000'000'000ULL;
inline std::map<VkDevice, bool> calibrated_devices;
inline bool pending_calibrated=false;
inline std::map<VkDevice, VkPhysicalDevice> physical_devices;
inline std::map<VkDevice, Json> device_reports;
inline std::map<VkDevice, std::string> device_scope_errors;
inline std::string pending_device_scope_error;
inline VkDevice measured_device=VK_NULL_HANDLE;
struct OwnedFence {VkDevice device;VkFence fence;};
inline std::vector<OwnedFence> owned_fences;

inline void WriteReport()
{
    if(!enabled || output_file.empty())return;
    std::ofstream output(output_file,std::ios::binary|std::ios::trunc);
    if(!(output<<report.dump(2)))throw std::runtime_error("Cannot write GPU measurement report: "+output_file);
}

[[noreturn]] inline void Fail(const std::string& code,const std::string& message)
{
    report["status"]="failed";
    report["error"]={{"code",code},{"message",message}};
    WriteReport();
    throw std::runtime_error("GPU profiling "+code+": "+message);
}

inline void Check(VkResult result,const char* operation)
{
    if(result!=VK_SUCCESS)Fail(result==VK_TIMEOUT?"gpu_timeout":"gpu_vulkan_error",
        std::string(operation)+" returned VkResult "+std::to_string(int(result)));
}

// Optional counter alignment around one ordinary selected frame.
#include "gui_frame_counters.h"

inline void Load(const std::string& filename)
{
    enabled=!filename.empty();
    if(!enabled){counter_window_enabled=false;return;}
    GFXRECON_LOG_INFO("GfxReconGui frame GPU profiling protocol 1");
    std::ifstream input(filename);
    if(!input)throw std::runtime_error("Cannot read GPU measurement request: "+filename);
    request=Json::parse(input);
    output_file=request.value("output_file",std::string{});
    report={{"protocol",1},{"mode","isolated-frame"},{"status","running"},{"error",nullptr},
            {"request_id",request.value("request_id",std::string{})},
            {"capture_sha256",request.value("capture_sha256",std::string{})},
            {"backend_sha256",request.value("backend_sha256",std::string{})},
            {"samples",Json::array()},{"overrides",Json::array()}};
    if(output_file.empty())throw std::runtime_error("GPU measurement request requires output_file");
    if(request.value("protocol",0)!=1 || request.value("mode",std::string{})!="isolated-frame")
        Fail("gpu_protocol_unsupported","Only protocol 1 isolated-frame measurement is supported");
    timeout_ns=request.value("timeout_ns",uint64_t(30'000'000'000ULL));
    if(timeout_ns==0 || timeout_ns>300'000'000'000ULL)Fail("gpu_invalid_request","timeout_ns must be in (0, 300 seconds]");
    if(!request.contains("frames") || !request["frames"].is_array() || request["frames"].empty())
        Fail("gpu_invalid_request","A non-empty frame plan is required");
    uint64_t last=0;
    for(const auto& frame:request["frames"]){
        const auto blocks=frame.at("submit_blocks").get<std::vector<uint64_t>>();
        if(blocks.empty() || blocks.front()!=frame.at("first_submit").get<uint64_t>() ||
           blocks.back()!=frame.at("last_submit").get<uint64_t>() || blocks.front()<=last ||
           !std::is_sorted(blocks.begin(),blocks.end()) || std::adjacent_find(blocks.begin(),blocks.end())!=blocks.end())
            Fail("gpu_invalid_request","Frame submission plan is not strictly ordered");
        last=blocks.back();
        Json sample=frame;
        sample["valid"]=false;sample["error"]="not_measured";
        sample["execution_mode"]="isolated-frame";
        sample["start_stage"]="TOP_OF_PIPE";sample["end_stage"]="BOTTOM_OF_PIPE";
        sample["completion"]="owned_fence";
        sample["scope"]="First through last planned GPU submission; may include dependency waits and CPU submission gaps between endpoints; waits gating the start marker are excluded, as are state load, query setup, host completion and presentation outside those submissions";
        sample["synchronization"]={{"entry","owned fence drain of all known command queues"},
                                   {"boundaries","ALL_COMMANDS execution barriers outside render passes"},
                                   {"per_submit_idle",false}};
        report["samples"].push_back(std::move(sample));
    }
    for(const auto& override_request:request.value("overrides",Json::array())){
        Json evidence=override_request;evidence["applied"]=false;evidence["creation_result"]=nullptr;
        evidence["execution_coverage"]="unknown";report["overrides"].push_back(std::move(evidence));
    }
    calibrated_devices.clear();physical_devices.clear();device_reports.clear();device_scope_errors.clear();
    pending_device_scope_error.clear();measured_device=VK_NULL_HANDLE;
    CounterWindowLoad();
    WriteReport();
}

// Called from the constructor branch that creates the actual swapchain class.
// CPU-only perf can consume the log; GPU requests require structured evidence.
inline void SwapchainSelected(const char* effective)
{
    GFXRECON_LOG_INFO("GfxReconGui effective swapchain mode: %s",effective);
    if(!enabled)return;
    const auto requested=request.value("swapchain_mode",std::string("virtual"));
    report["swapchain"]={{"requested",requested},{"effective",effective},{"source","backend_swapchain_class"},
        {"real_swapchain_acquisition",std::string(effective)!="offscreen"}};
    if(requested!=effective)Fail("gpu_swapchain_mismatch","Requested and effective replay swapchain modes differ");
    WriteReport();
}

template<class InstanceTable,class InstanceInfo>
inline void ConfigureDevice(const InstanceTable* table,const InstanceInfo& instance_info,VkPhysicalDevice physical,
                            VkDeviceCreateInfo& create_info,std::vector<const char*>& extensions)
{
    if(!enabled)return;
    pending_device_scope_error.clear();
    for(uint32_t i=0;i<create_info.queueCreateInfoCount;++i)
        if(create_info.pQueueCreateInfos[i].flags & VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT)
            pending_device_scope_error="Protected queues are not supported for GPU profiling";
    for(auto* next=static_cast<const VkBaseInStructure*>(create_info.pNext);next;next=next->pNext)
        if(next->sType==VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO)
            pending_device_scope_error="Device groups are not supported for GPU profiling";
    uint32_t count=0;
    Check(table->EnumerateDeviceExtensionProperties(physical,nullptr,&count,nullptr),"vkEnumerateDeviceExtensionProperties(count)");
    std::vector<VkExtensionProperties> available(count);
    Check(table->EnumerateDeviceExtensionProperties(physical,nullptr,&count,available.data()),"vkEnumerateDeviceExtensionProperties");
    const char* calibrated=nullptr;
    for(const auto& extension:available)
        if(std::string(extension.extensionName)==VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME){calibrated=VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME;break;}
    if(!calibrated)for(const auto& extension:available)
        if(std::string(extension.extensionName)==VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME){calibrated=VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME;break;}
    // Both calibrated-timestamp extensions depend on properties2 or Vulkan 1.1.
    // Inactive Vulkan 1.0 snapshot devices need no instrumentation extensions.
    if(instance_info.api_version<VK_API_VERSION_1_1 &&
       !instance_info.IsEnabledExtension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
        calibrated=nullptr;
    if(calibrated && std::none_of(extensions.begin(),extensions.end(),[&](const char* name){return std::string(name)==calibrated;}))
        extensions.push_back(calibrated);
    create_info.enabledExtensionCount=uint32_t(extensions.size());
    create_info.ppEnabledExtensionNames=extensions.data();
    pending_calibrated=calibrated!=nullptr;
    pending_calibrated_extension=calibrated?calibrated:"";
}

template<class InstanceTable,class InstanceInfo>
inline void CreatedDevice(const InstanceTable* table,const InstanceInfo& instance_info,
                          VkPhysicalDevice physical,VkDevice device,VkResult result)
{
    if(!enabled)return;
    Check(result,"vkCreateDevice/post-create");
    physical_devices[device]=physical;
    device_scope_errors[device]=pending_device_scope_error;
    calibrated_devices[device]=pending_calibrated;
    calibrated_extensions[device]=pending_calibrated_extension;
    VkPhysicalDeviceProperties properties{};
    table->GetPhysicalDeviceProperties(physical,&properties);
    std::ostringstream uuid;uuid<<std::hex<<std::setfill('0');
    for(auto value:properties.pipelineCacheUUID)uuid<<std::setw(2)<<unsigned(value);
    Json device_report{{"device_name",properties.deviceName},{"vendor_id",properties.vendorID},
        {"device_id",properties.deviceID},{"api_version",properties.apiVersion},
        {"driver_version",properties.driverVersion},{"pipeline_cache_uuid",uuid.str()},
        {"driver_name",nullptr},{"driver_info",nullptr},{"calibrated_timestamps_enabled",calibrated_devices[device]}};
    PFN_vkGetPhysicalDeviceProperties2 get_properties2=nullptr;
    if(instance_info.api_version>=VK_API_VERSION_1_1 &&
       table->GetPhysicalDeviceProperties2!=gfxrecon::graphics::noop::vkGetPhysicalDeviceProperties2)
        get_properties2=table->GetPhysicalDeviceProperties2;
    else if(instance_info.IsEnabledExtension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME) &&
            table->GetPhysicalDeviceProperties2KHR!=gfxrecon::graphics::noop::vkGetPhysicalDeviceProperties2KHR)
        get_properties2=table->GetPhysicalDeviceProperties2KHR;
    device_report["instance_api_version"]=instance_info.api_version;
    if(properties.apiVersion>=VK_API_VERSION_1_2 && get_properties2){
        VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,&driver};
        get_properties2(physical,&props2);
        device_report["driver_name"]=driver.driverName;device_report["driver_info"]=driver.driverInfo;
        device_report["driver_id"]=driver.driverID;
    }
    // Trimmed state may create inactive logical devices. Select provenance only
    // when the first planned submission identifies the actual measured device.
    device_reports[device]=std::move(device_report);
}

inline void ShaderCreated(uint64_t module,const void* bytes,size_t size,VkResult result,bool replacement_selected)
{
    if(!enabled)return;
    for(auto& evidence:report["overrides"]){
        if(evidence.at("module").get<uint64_t>()!=module)continue;
        const auto actual=ShaderSha256(bytes,size);
        evidence["actual_sha256"]=actual;evidence["creation_result"]=int(result);
        evidence["replacement_selected"]=replacement_selected;
        evidence["applied"]=replacement_selected && result==VK_SUCCESS && actual==evidence.at("sha256").get<std::string>();
        WriteReport();
        if(!evidence["applied"].get<bool>())Fail("gpu_override_not_applied","Shader module "+std::to_string(module)+" replacement bytes or creation result did not match");
    }
}

struct FrameResources {
    size_t sample_index=0,next_submit=0;
    VkDevice device=VK_NULL_HANDLE;
    VkQueue queue=VK_NULL_HANDLE;
    uint64_t queue_id=0;
    VkCommandPool pool=VK_NULL_HANDLE;
    VkQueryPool queries=VK_NULL_HANDLE;
    VkCommandBuffer start=VK_NULL_HANDLE,end=VK_NULL_HANDLE;
    uint32_t valid_bits=0;
    double period=0;
    Clock::time_point entry_time{};
};
inline FrameResources active;

inline void ForgetFence(VkDevice device,VkFence fence)
{
    owned_fences.erase(std::remove_if(owned_fences.begin(),owned_fences.end(),
        [&](const OwnedFence& owned){return owned.device==device&&owned.fence==fence;}),owned_fences.end());
}

inline void BeforeDeviceRelease(VkDevice device)
{
    if(!enabled)return;
    // A captured destroy cannot remove a device while an incomplete planned
    // interval still owns in-flight objects. Abort before the actual destroy;
    // consumer teardown first waits for completion, then releases those objects.
    if(active.device==device || std::any_of(owned_fences.begin(),owned_fences.end(),
        [&](const OwnedFence& owned){return owned.device==device;}))
        Fail("gpu_scope_unsupported","Replay destroyed a device before its planned GPU interval completed");
    device_reports.erase(device);physical_devices.erase(device);device_scope_errors.erase(device);calibrated_devices.erase(device);
}

inline uint64_t Remaining(Clock::time_point deadline)
{
    const auto now=Clock::now();
    if(now>=deadline)Fail("gpu_timeout","GPU completion deadline exceeded");
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(deadline-now).count());
}

template<class DeviceTable>
inline void WaitFence(const DeviceTable* table,VkDevice device,VkFence fence,Clock::time_point deadline)
{
    Check(table->WaitForFences(device,1,&fence,VK_TRUE,Remaining(deadline)),"vkWaitForFences");
}

template<class ObjectTable,class GetDeviceTable>
inline void DrainQueues(const ObjectTable& objects,GetDeviceTable get_table,Clock::time_point deadline)
{
    struct Drain {VkDevice device;VkFence fence;};
    std::vector<Drain> drains;
    // Submit every drain before waiting so queues with interdependencies can progress.
    objects.VisitVkQueueInfo([&](const auto* queue){
        if(queue->handle==VK_NULL_HANDLE)return;
        auto table=get_table(queue->parent);
        VkFenceCreateInfo create{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence=VK_NULL_HANDLE;
        Check(table->CreateFence(queue->parent,&create,nullptr,&fence),"vkCreateFence(entry)");
        owned_fences.push_back({queue->parent,fence});
        drains.push_back({queue->parent,fence});
        Check(table->QueueSubmit(queue->handle,0,nullptr,fence),"vkQueueSubmit(entry drain)");
    });
    for(const auto& drain:drains){
        auto table=get_table(drain.device);
        WaitFence(table,drain.device,drain.fence,deadline);
        table->DestroyFence(drain.device,drain.fence,nullptr);
        ForgetFence(drain.device,drain.fence);
    }
}

template<class DeviceTable>
inline void SubmitOwned(const DeviceTable* table,VkDevice device,VkQueue queue,VkCommandBuffer command,Clock::time_point deadline)
{
    VkFenceCreateInfo create{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence=VK_NULL_HANDLE;
    Check(table->CreateFence(device,&create,nullptr,&fence),"vkCreateFence(owned)");
    owned_fences.push_back({device,fence});
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    if(command){submit.commandBufferCount=1;submit.pCommandBuffers=&command;}
    Check(table->QueueSubmit(queue,1,&submit,fence),"vkQueueSubmit(owned)");
    WaitFence(table,device,fence,deadline);
    table->DestroyFence(device,fence,nullptr);
    ForgetFence(device,fence);
}

template<class DeviceTable>
inline void Cleanup(const DeviceTable* table)
{
    if(active.queries)table->DestroyQueryPool(active.device,active.queries,nullptr);
    if(active.pool)table->DestroyCommandPool(active.device,active.pool,nullptr);
    active=FrameResources{};
}

template<class GetDeviceTable>
inline void ReleaseAfterIdle(GetDeviceTable get_table) noexcept
{
    if(!enabled)return;
    // Called only after the existing consumer-wide idle wait has returned.
    // On a timeout/error we retained resources until this safe teardown point.
    if(active.device)Cleanup(get_table(active.device));
    ReleaseClockBracketsAfterIdle(get_table);
    for(const auto& owned:owned_fences)
        get_table(owned.device)->DestroyFence(owned.device,owned.fence,nullptr);
    owned_fences.clear();
}

template<class DeviceTable,class InstanceTable,class ObjectTable,class GetDeviceTable>
inline void Begin(size_t sample_index,const DeviceTable* table,const InstanceTable* instance,
                  VkPhysicalDevice physical,const ObjectTable& objects,GetDeviceTable get_table,
                  VkDevice device,VkQueue queue,uint64_t queue_id,uint32_t family)
{
    if(active.device)Fail("gpu_plan_mismatch","A previous measured frame did not finish");
    if(measured_device && measured_device!=device)
        Fail("gpu_scope_unsupported","Selected GPU frame samples use multiple active logical devices");
    measured_device=device;
    if(!device_reports.count(device))Fail("gpu_plan_mismatch","Measured replay device has no runtime property record");
    if(!device_scope_errors[device].empty())Fail("gpu_scope_unsupported",device_scope_errors[device]);
    report["device"]=device_reports.at(device);
    active.sample_index=sample_index;active.device=device;active.queue=queue;active.queue_id=queue_id;
    active.entry_time=Clock::now();
    const auto deadline=active.entry_time+std::chrono::nanoseconds(timeout_ns);
    DrainQueues(objects,get_table,deadline);
    VkPhysicalDeviceProperties properties{};instance->GetPhysicalDeviceProperties(physical,&properties);
    uint32_t count=0;instance->GetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);
    std::vector<VkQueueFamilyProperties> families(count);instance->GetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());
    if(family>=families.size() || families[family].timestampValidBits==0)
        Fail("gpu_timestamp_unsupported","The actual replay queue does not support timestamps");
    if(!(families[family].queueFlags & (VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)))
        Fail("gpu_scope_unsupported","The initial frame profiling implementation requires a graphics or compute queue");
    active.valid_bits=families[family].timestampValidBits;active.period=properties.limits.timestampPeriod;
    if(active.valid_bits>64 || !std::isfinite(active.period) || active.period<=0)
        Fail("gpu_timestamp_unsupported","Invalid replay timestamp properties");
    auto& sample=report["samples"][sample_index];
    sample["queue_family_index"]=family;sample["timestamp_valid_bits"]=active.valid_bits;
    sample["timestamp_period_ns"]=active.period;
    sample["clock_domain"]=calibrated_devices[device]?"calibrated_device":"same_submission";
    sample["timestamp_support"]=true;
    if(sample["first_submit"]!=sample["last_submit"] && !calibrated_devices[device])
        Fail("gpu_timestamp_domain_unsupported","Measured endpoint submissions have no enabled comparable clock domain");
    VkQueryPoolCreateInfo query_create{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};query_create.queryType=VK_QUERY_TYPE_TIMESTAMP;query_create.queryCount=2;
    Check(table->CreateQueryPool(device,&query_create,nullptr,&active.queries),"vkCreateQueryPool");
    VkCommandPoolCreateInfo pool_create{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool_create.queueFamilyIndex=family;
    Check(table->CreateCommandPool(device,&pool_create,nullptr,&active.pool),"vkCreateCommandPool");
    VkCommandBuffer commands[3]{};
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool=active.pool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=3;
    Check(table->AllocateCommandBuffers(device,&allocate,commands),"vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    // Reset completes before the start marker; it is outside the reported interval.
    Check(table->BeginCommandBuffer(commands[0],&begin),"vkBeginCommandBuffer(reset)");
    table->CmdResetQueryPool(commands[0],active.queries,0,2);
    Check(table->EndCommandBuffer(commands[0]),"vkEndCommandBuffer(reset)");
    SubmitOwned(table,device,queue,commands[0],deadline);
    active.start=commands[1];active.end=commands[2];
    Check(table->BeginCommandBuffer(active.start,&begin),"vkBeginCommandBuffer(start)");
    table->CmdWriteTimestamp(active.start,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,active.queries,0);
    // Explicitly order subsequent work behind the timestamp. Array order alone
    // does not imply execution ordering across command buffers or submissions.
    table->CmdPipelineBarrier(active.start,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,0,nullptr);
    Check(table->EndCommandBuffer(active.start),"vkEndCommandBuffer(start)");
    Check(table->BeginCommandBuffer(active.end,&begin),"vkBeginCommandBuffer(end)");
    table->CmdPipelineBarrier(active.end,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,0,nullptr);
    table->CmdWriteTimestamp(active.end,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,active.queries,1);
    Check(table->EndCommandBuffer(active.end),"vkEndCommandBuffer(end)");
    if(counter_window_enabled){
        AwaitCounterReady();
        active.entry_time=Clock::now(); // Collector startup is outside timestamp completion/wrap bounds.
    }
    WriteReport();
}

template<class DeviceTable,class InstanceTable>
inline void Complete(const DeviceTable* table,const InstanceTable* instance,VkPhysicalDevice physical)
{
    const auto deadline=active.entry_time+std::chrono::nanoseconds(timeout_ns);
    SubmitOwned(table,active.device,active.queue,VK_NULL_HANDLE,deadline);
    struct Value {uint64_t tick,available;};Value values[2]{};
    Check(table->GetQueryPoolResults(active.device,active.queries,0,2,sizeof(values),values,sizeof(Value),
          VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT),"vkGetQueryPoolResults");
    if(!values[0].available || !values[1].available)Fail("gpu_query_unavailable","Completed frame has unavailable timestamp results");
    const uint64_t mask=active.valid_bits==64?std::numeric_limits<uint64_t>::max():((uint64_t(1)<<active.valid_bits)-1);
    const auto start=values[0].tick&mask,end=values[1].tick&mask;
    const auto elapsed_ticks=(end-start)&mask;
    const long double wrap_ns=std::ldexp(static_cast<long double>(active.period),int(active.valid_bits));
    const auto host_bound=std::chrono::duration<long double,std::nano>(Clock::now()-active.entry_time).count();
    if(host_bound>=wrap_ns)Fail("gpu_timestamp_wrap_ambiguous","The bounded sample may span a full timestamp wrap period");
    const double elapsed_ns=double(elapsed_ticks)*active.period;
    if(!std::isfinite(elapsed_ns) || elapsed_ns<=0 || elapsed_ns>double(host_bound)+active.period*2)
        Fail("gpu_timestamp_invalid","GPU timestamp duration is zero, nonfinite or exceeds its completion bound");
    auto& sample=report["samples"][active.sample_index];
    sample["start_tick"]=start;sample["end_tick"]=end;sample["elapsed_ns"]=elapsed_ns;
    sample["elapsed_ms"]=elapsed_ns/1e6;sample["valid"]=true;sample["error"]=nullptr;
    sample["start_available"]=values[0].available!=0;sample["end_available"]=values[1].available!=0;
    sample["wrap_detected"]=end<start;sample["host_completion_bound_ns"]=double(host_bound);
    CorrelateCounterWindow(table,instance,physical,active.device,active.queue,active.queue_id,
        sample.at("queue_family_index").get<uint32_t>(),active.valid_bits,active.period,start,end,sample);
    Cleanup(table);
    bool complete=true;
    for(const auto& item:report["samples"])complete=complete&&item.value("valid",false);
    if(complete){
        for(const auto& evidence:report["overrides"])
            if(!evidence.value("applied",false))Fail("gpu_override_not_applied","A requested shader replacement lacks successful creation evidence");
        report["status"]="complete";
    }
    WriteReport();
}

inline size_t SampleFor(uint64_t block)
{
    for(size_t i=0;i<report["samples"].size();++i){
        const auto first=report["samples"][i].at("first_submit").get<uint64_t>();
        const auto last=report["samples"][i].at("last_submit").get<uint64_t>();
        if(block>=first&&block<=last)return i;
    }
    return size_t(-1);
}

template<class Submit,class DeviceTable,class InstanceTable,class ObjectTable,class GetDeviceTable,class Function>
inline VkResult SubmitFrame(Function function,const DeviceTable* table,const InstanceTable* instance,
                           const ObjectTable& objects,GetDeviceTable get_table,VkPhysicalDevice physical,
                           VkDevice device,VkQueue queue,uint64_t queue_id,uint32_t family,uint64_t block,
                           uint32_t count,const Submit* submissions,VkFence application_fence)
{
    if(!enabled)return function(queue,count,submissions,application_fence);
    const auto sample_index=SampleFor(block);
    if(sample_index==size_t(-1)){
        if(active.device)Fail("gpu_plan_mismatch","Unplanned submission occurred inside a measured frame");
        return function(queue,count,submissions,application_fence);
    }
    const auto& plan=report["samples"][sample_index];
    if(queue_id!=plan.at("queue_id").get<uint64_t>())Fail("gpu_plan_mismatch","Replay queue differs from requested frame plan");
    if(count==0 || !submissions)Fail("gpu_scope_unsupported","Empty GPU submission calls are not profiled");
    if(count>1 && !calibrated_devices[device])
        Fail("gpu_timestamp_domain_unsupported","Timestamp endpoints in separate submission batches require calibrated timestamps");
    for(uint32_t i=0;i<count;++i){
        if(submissions[i].pNext)Fail("gpu_scope_unsupported","Submission extension chains require a dedicated profiling implementation");
        if constexpr(std::is_same_v<Submit,VkSubmitInfo2>){
            if(submissions[i].flags)Fail("gpu_scope_unsupported","Protected or flagged SubmitInfo2 is not supported");
            for(uint32_t j=0;j<submissions[i].commandBufferInfoCount;++j)
                if(submissions[i].pCommandBufferInfos[j].pNext || (submissions[i].pCommandBufferInfos[j].deviceMask & ~uint32_t(1)))
                    Fail("gpu_scope_unsupported","Extended or device-group command-buffer submissions are not supported");
        }
    }
    if(block==plan.at("first_submit").get<uint64_t>())
        Begin(sample_index,table,instance,physical,objects,get_table,device,queue,queue_id,family);
    if(active.device!=device || active.queue!=queue || active.sample_index!=sample_index)
        Fail("gpu_plan_mismatch","Measured frame did not start on its planned device and queue");
    const auto& blocks=plan.at("submit_blocks");
    if(active.next_submit>=blocks.size() || blocks[active.next_submit].get<uint64_t>()!=block)
        Fail("gpu_plan_mismatch","Replay submission order differs from the profiling plan");
    ++active.next_submit;
    const bool first=block==plan.at("first_submit").get<uint64_t>();
    const bool last=block==plan.at("last_submit").get<uint64_t>();
    std::vector<Submit> modified(submissions,submissions+count);
    std::vector<std::vector<VkCommandBuffer>> buffers(count);
    std::vector<std::vector<VkCommandBufferSubmitInfo>> infos(count);
    for(uint32_t i=0;i<count;++i){
        if constexpr(std::is_same_v<Submit,VkSubmitInfo>){
            if(first&&i==0)buffers[i].push_back(active.start);
            for(uint32_t j=0;j<modified[i].commandBufferCount;++j)buffers[i].push_back(modified[i].pCommandBuffers[j]);
            if(last&&i==count-1)buffers[i].push_back(active.end);
            modified[i].commandBufferCount=uint32_t(buffers[i].size());modified[i].pCommandBuffers=buffers[i].data();
        }else{
            auto marker=[](VkCommandBuffer command){VkCommandBufferSubmitInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};info.commandBuffer=command;info.deviceMask=1;return info;};
            if(first&&i==0)infos[i].push_back(marker(active.start));
            for(uint32_t j=0;j<modified[i].commandBufferInfoCount;++j)infos[i].push_back(modified[i].pCommandBufferInfos[j]);
            if(last&&i==count-1)infos[i].push_back(marker(active.end));
            modified[i].commandBufferInfoCount=uint32_t(infos[i].size());modified[i].pCommandBufferInfos=infos[i].data();
        }
    }
    const auto result=function(queue,count,modified.data(),application_fence);
    Check(result,"captured GPU submission");
    if(last){
        if(active.next_submit!=blocks.size())Fail("gpu_plan_mismatch","Measured frame ended before all planned submissions");
        Complete(table,instance,physical);
    }
    return result;
}

inline void Finish() noexcept
{
    if(!enabled)return;
    try{
        if(report.value("status",std::string{})=="running"){
            report["status"]="failed";
            report["error"]={{"code","gpu_incomplete"},{"message","Replay ended before all planned GPU samples and shader replacements completed"}};
        }
        WriteReport();
    }catch(...){ }
}
} // namespace gfx_gui_gpu
