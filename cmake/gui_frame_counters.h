#pragma once
// Included inside gfx_gui_gpu after the GPU error helpers. Counter correlation
// failures use local exceptions and never invalidate completed GPU timestamps.
inline bool counter_window_enabled=false,counter_resume_received=false;
inline uint64_t counter_entry_boot_ns=0;
inline std::map<VkDevice,std::string> calibrated_extensions;
inline std::string pending_calibrated_extension;
struct ClockBracketResources {
    VkDevice device=VK_NULL_HANDLE;
    VkQueryPool queries=VK_NULL_HANDLE;
    VkCommandPool pool=VK_NULL_HANDLE;
    VkFence fence=VK_NULL_HANDLE;
};
inline std::vector<ClockBracketResources> clock_bracket_resources;
[[noreturn]] inline void CounterFail(const std::string& code,const std::string& message){throw std::runtime_error(code+": "+message);}
inline void CounterCheck(VkResult result,const char* operation){if(result!=VK_SUCCESS)CounterFail("counter_clock_error",std::string(operation)+" returned VkResult "+std::to_string(int(result)));}
inline uint64_t BootNs(){
#if defined(__linux__)
    timespec now{};if(clock_gettime(CLOCK_BOOTTIME,&now)!=0)CounterFail("counter_clock_unavailable","CLOCK_BOOTTIME could not be read");
    return uint64_t(now.tv_sec)*1000000000ULL+uint64_t(now.tv_nsec);
#else
    CounterFail("counter_clock_unavailable","CLOCK_BOOTTIME is unavailable on this platform");
#endif
}
inline void CounterWindowLoad(){
    counter_window_enabled=request.contains("counter_window");counter_resume_received=false;counter_entry_boot_ns=0;calibrated_extensions.clear();
    if(request.contains("sustained"))Fail("gpu_protocol_unsupported","Inner-loop sustained replay has been removed; use fresh outer replays");
    if(!counter_window_enabled)return;
#if !defined(__ANDROID__)
    Fail("gpu_counter_window_unsupported","Frame counter windows currently require Android");
#endif
    const auto& options=request.at("counter_window");
    const auto period=options.value("period_ns",uint64_t(0)),ready_timeout=options.value("ready_timeout_ns",uint64_t(60000000000ULL));
    if(options.value("protocol",0)!=1||request.at("frames").size()!=1||!period||period>1000000000ULL||!ready_timeout||ready_timeout>120000000000ULL||options.value("ready_file",std::string{}).empty()||options.value("continue_file",std::string{}).empty())Fail("gpu_invalid_request","Counter window protocol 1 requires one frame, a valid period, bounded ready timeout and handshake files");
    GFXRECON_LOG_INFO("GfxReconGui frame counter window protocol 1");
    report["counter_window"]={{"protocol",1},{"status","unavailable"},{"frame",request.at("frames").front().at("frame")},{"period_ns",period},{"start_boot_ns",nullptr},{"end_boot_ns",nullptr},{"clock_bridge",{{"status","unavailable"}}},{"reason","not_measured"}};
}
inline void AwaitCounterReady(){
    if(!counter_window_enabled)return;
    try{
    const auto& options=request.at("counter_window");const auto ready=options.at("ready_file").get<std::string>(),temporary=ready+".partial";
    {std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);if(!(stream<<Json{{"request_id",request.at("request_id")},{"status","ready"},{"frame",request.at("frames").front().at("frame")}}.dump(2)))CounterFail("counter_handshake","Cannot write counter ready file");}
    if(std::rename(temporary.c_str(),ready.c_str())!=0)CounterFail("counter_handshake","Cannot publish counter ready file");
    const auto deadline=Clock::now()+std::chrono::nanoseconds(options.value("ready_timeout_ns",uint64_t(60000000000ULL)));
    for(;;){
        std::ifstream input(options.at("continue_file").get<std::string>(),std::ios::binary);
        if(input){try{const auto signal=Json::parse(input);if(signal.value("request_id",std::string{})==request.at("request_id").get<std::string>()&&signal.value("status",std::string{})=="continue"){counter_resume_received=true;break;}}catch(const Json::exception&){} }
        if(Clock::now()>=deadline)CounterFail("counter_handshake_timeout","Counter resume token was not received before the bounded timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    counter_entry_boot_ns=BootNs();
    }catch(const std::exception& error){report["counter_window"]["reason"]=error.what();}
}
template<class InstanceTable,class DeviceTable>
inline Json CalibrateBoot(const InstanceTable* instance,const DeviceTable* table,VkPhysicalDevice physical,VkDevice device)
{
#if defined(__linux__)
    PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR domains=nullptr;
    PFN_vkGetCalibratedTimestampsKHR timestamps=nullptr;
    if(calibrated_extensions[device]==VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME){
        if(instance->GetPhysicalDeviceCalibrateableTimeDomainsKHR!=gfxrecon::graphics::noop::vkGetPhysicalDeviceCalibrateableTimeDomainsKHR)
            domains=instance->GetPhysicalDeviceCalibrateableTimeDomainsKHR;
        if(table->GetCalibratedTimestampsKHR!=gfxrecon::graphics::noop::vkGetCalibratedTimestampsKHR)
            timestamps=table->GetCalibratedTimestampsKHR;
    }else{
        if(instance->GetPhysicalDeviceCalibrateableTimeDomainsEXT!=gfxrecon::graphics::noop::vkGetPhysicalDeviceCalibrateableTimeDomainsEXT)
            domains=instance->GetPhysicalDeviceCalibrateableTimeDomainsEXT;
        if(table->GetCalibratedTimestampsEXT!=gfxrecon::graphics::noop::vkGetCalibratedTimestampsEXT)
            timestamps=table->GetCalibratedTimestampsEXT;
    }
    if(!domains||!timestamps)return {{"status","unavailable"},{"reason","calibrated_timestamp_dispatch_unavailable"}};
    uint32_t count=0;CounterCheck(domains(physical,&count,nullptr),"vkGetPhysicalDeviceCalibrateableTimeDomains(count)");
    std::vector<VkTimeDomainKHR> available(count);CounterCheck(domains(physical,&count,available.data()),"vkGetPhysicalDeviceCalibrateableTimeDomains");
    VkTimeDomainKHR host=VK_TIME_DOMAIN_DEVICE_KHR;clockid_t host_clock=CLOCK_MONOTONIC;
    for(const auto candidate:available)if(candidate==VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR){host=candidate;break;}
    if(host==VK_TIME_DOMAIN_DEVICE_KHR)for(const auto candidate:available)
        if(candidate==VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR){host=candidate;host_clock=CLOCK_MONOTONIC_RAW;break;}
    if(host==VK_TIME_DOMAIN_DEVICE_KHR)return {{"status","unavailable"},{"reason","no_calibrated_host_domain"}};
    const uint64_t boot_before=BootNs();timespec host_now{};
    if(clock_gettime(host_clock,&host_now)!=0)CounterFail("gpu_clock_unavailable","Calibrated host clock could not be read");
    const uint64_t boot_after=BootNs(),host_ns=uint64_t(host_now.tv_sec)*1000000000ULL+uint64_t(host_now.tv_nsec);
    const int64_t offset=int64_t(boot_before+(boot_after-boot_before)/2)-int64_t(host_ns);
    VkCalibratedTimestampInfoKHR infos[2]={{VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,nullptr,VK_TIME_DOMAIN_DEVICE_KHR},
                                         {VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,nullptr,host}};
    uint64_t values[2]{},deviation=0;CounterCheck(timestamps(device,2,infos,values,&deviation),"vkGetCalibratedTimestamps");
    return {{"status","available"},{"counter_domain","BOOTTIME"},{"host_domain",host==VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR?"CLOCK_MONOTONIC":"CLOCK_MONOTONIC_RAW"},
        {"gpu_tick",values[0]},{"host_timestamp_ns",values[1]},{"host_to_boot_offset_ns",offset},
        {"max_deviation_ns",deviation},{"clock_read_uncertainty_ns",(boot_after-boot_before+1)/2},
        {"uncertainty_ns",deviation+(boot_after-boot_before+1)/2},{"observed_boot_ns",boot_after}};
#else
    (void)instance;(void)table;(void)physical;(void)device;
    return {{"status","unavailable"},{"reason","boottime_clock_not_available_on_this_platform"}};
#endif
}

inline long double RelativeQueryNs(uint64_t tick,uint64_t reference,uint32_t bits,double period)
{
    const uint64_t mask=bits==64?UINT64_MAX:((uint64_t(1)<<bits)-1);
    const uint64_t forward=(tick-reference)&mask;
    return (forward>mask/2?-static_cast<long double>((reference-tick)&mask):static_cast<long double>(forward))*period;
}

template<class GetDeviceTable>
inline void ReleaseClockBracketsAfterIdle(GetDeviceTable get_table) noexcept
{
    for(const auto& resources:clock_bracket_resources){
        const auto* table=get_table(resources.device);
        if(resources.queries)table->DestroyQueryPool(resources.device,resources.queries,nullptr);
        if(resources.pool)table->DestroyCommandPool(resources.device,resources.pool,nullptr);
        // Fences remain in the common owned-fence list and are released there.
    }
    clock_bracket_resources.clear();
}

template<class DeviceTable>
inline Json QueueQueryBootBracket(const DeviceTable* table,VkDevice device,VkQueue queue,uint64_t queue_id,
                                 uint32_t family,uint32_t bits,double period,Clock::time_point deadline,
                                 const Json& driver_calibration)
{
#if !defined(__linux__)
    (void)table;(void)device;(void)queue;(void)queue_id;(void)family;(void)bits;(void)period;(void)deadline;(void)driver_calibration;
    CounterFail("gpu_clock_unavailable","Owned queue clock correlation requires Android/Linux CLOCK_BOOTTIME");
#else
    if(!calibrated_devices[device])CounterFail("gpu_clock_unavailable","A comparable query timestamp domain across submissions is required");
    clock_bracket_resources.emplace_back();
    const size_t resource_index=clock_bracket_resources.size()-1;
    auto& resources=clock_bracket_resources.back();resources.device=device;
    VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};query.queryType=VK_QUERY_TYPE_TIMESTAMP;query.queryCount=3;
    CounterCheck(table->CreateQueryPool(device,&query,nullptr,&resources.queries),"vkCreateQueryPool(clock bracket)");
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.queueFamilyIndex=family;
    CounterCheck(table->CreateCommandPool(device,&pool,nullptr,&resources.pool),"vkCreateCommandPool(clock bracket)");
    VkCommandBuffer commands[3]{};VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool=resources.pool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=3;
    CounterCheck(table->AllocateCommandBuffers(device,&allocate,commands),"vkAllocateCommandBuffers(clock bracket)");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    CounterCheck(table->CreateFence(device,&fence,nullptr,&resources.fence),"vkCreateFence(clock bracket)");
    owned_fences.push_back({device,resources.fence});
    for(uint32_t i=0;i<3;++i){
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CounterCheck(table->BeginCommandBuffer(commands[i],&begin),"vkBeginCommandBuffer(clock bracket)");
        table->CmdResetQueryPool(commands[i],resources.queries,i,1);
        table->CmdPipelineBarrier(commands[i],VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,0,nullptr);
        table->CmdWriteTimestamp(commands[i],VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,resources.queries,i);
        CounterCheck(table->EndCommandBuffer(commands[i]),"vkEndCommandBuffer(clock bracket)");
    }
    Json probes=Json::array();uint64_t best_width=UINT64_MAX;size_t best=0;
    const uint64_t mask=bits==64?UINT64_MAX:((uint64_t(1)<<bits)-1);
    long double offset_lower=-std::numeric_limits<long double>::infinity(),offset_upper=std::numeric_limits<long double>::infinity();
    uint64_t reference=0;
    for(uint32_t i=0;i<3;++i){
        if(i)CounterCheck(table->ResetFences(device,1,&resources.fence),"vkResetFences(clock bracket)");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&commands[i];
        const auto before=BootNs();
        CounterCheck(table->QueueSubmit(queue,1,&submit,resources.fence),"vkQueueSubmit(clock bracket)");
        const auto now=Clock::now();
        if(now>=deadline)CounterFail("gpu_timeout","Owned clock marker completion exceeded deadline");
        const auto remaining=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(deadline-now).count());
        CounterCheck(table->WaitForFences(device,1,&resources.fence,VK_TRUE,remaining),"vkWaitForFences(clock bracket)");
        const auto after=BootNs();
        struct Value{uint64_t tick,available;}value{};
        CounterCheck(table->GetQueryPoolResults(device,resources.queries,i,1,sizeof(value),&value,sizeof(value),
              VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT),"vkGetQueryPoolResults(clock bracket)");
        if(!value.available||after<before)CounterFail("gpu_clock_unavailable","Owned GPU marker is unavailable or host clock regressed");
        const auto tick=value.tick&mask;
        if(i==0)reference=tick;
        const auto relative_ns=RelativeQueryNs(tick,reference,bits,period);
        offset_lower=std::max(offset_lower,static_cast<long double>(before)-relative_ns-period);
        offset_upper=std::min(offset_upper,static_cast<long double>(after)-relative_ns+period);
        probes.push_back({{"gpu_tick",tick},{"marker_before_boot_ns",before},{"marker_after_boot_ns",after},
                          {"marker_query_available",true},{"completion","owned_fence"}});
        if(after-before<best_width){best_width=after-before;best=i;}
    }
    if(offset_lower>offset_upper){
        CounterFail("gpu_clock_inconsistent","Owned query marker offset bounds do not overlap at the declared timestamp period");
    }
    const auto& chosen=probes.at(best);
    const auto before=chosen.at("marker_before_boot_ns").get<uint64_t>(),after=chosen.at("marker_after_boot_ns").get<uint64_t>();
    const auto uncertainty=(after-before+1)/2+uint64_t(std::ceil(period));
    Json result{{"source","owned_queue_timestamp_bracket"},{"status","available"},{"counter_domain","BOOTTIME"},
        {"host_domain","CLOCK_BOOTTIME"},{"gpu_tick",chosen.at("gpu_tick")},{"host_timestamp_ns",before+(after-before)/2},
        {"host_to_boot_offset_ns",int64_t(0)},{"marker_before_boot_ns",before},{"marker_after_boot_ns",after},
        {"marker_query_available",true},{"completion","owned_fence"},{"timestamp_valid_bits",bits},{"timestamp_period_ns",period},
        {"queue_id",queue_id},{"uncertainty_ns",uncertainty},{"probe_count",probes.size()},
        {"offset_bounds_overlap",true},{"probes",probes},{"period_assumption","constant device timestampPeriod across the short marker/frame interval"},
        {"driver_calibration",driver_calibration}};
    if(driver_calibration.value("status",std::string{})=="available"){
        const auto point=driver_calibration.at("gpu_tick").get<uint64_t>()&mask;
        const auto mapped=static_cast<long double>(driver_calibration.at("host_timestamp_ns").get<uint64_t>())+
            driver_calibration.at("host_to_boot_offset_ns").get<int64_t>()+
            RelativeQueryNs(chosen.at("gpu_tick").get<uint64_t>(),point,bits,period);
        const auto claimed=driver_calibration.value("uncertainty_ns",uint64_t(0));
        const bool consistent=mapped+claimed>=before&&mapped<=static_cast<long double>(after)+claimed;
        result["driver_calibration"]["validation_status"]=consistent?"consistent_with_owned_query":"inconsistent_with_owned_query";
        result["driver_calibration"]["mapped_owned_marker_boot_ns"]=double(mapped);
    }
    table->DestroyFence(device,resources.fence,nullptr);
    owned_fences.erase(std::remove_if(owned_fences.begin(),owned_fences.end(),[&](const OwnedFence& f){return f.device==device&&f.fence==resources.fence;}),owned_fences.end());
    table->DestroyQueryPool(device,resources.queries,nullptr);table->DestroyCommandPool(device,resources.pool,nullptr);
    clock_bracket_resources.erase(clock_bracket_resources.begin()+resource_index);
    return result;
#endif
}

inline uint64_t TickToBoot(uint64_t tick,uint32_t bits,double period,const Json& calibration)
{
    const uint64_t mask=bits==64?UINT64_MAX:((uint64_t(1)<<bits)-1),reference=calibration.at("gpu_tick").get<uint64_t>()&mask;
    const uint64_t forward=(tick-reference)&mask;
    const long double delta=forward>mask/2?-static_cast<long double>((reference-tick)&mask):static_cast<long double>(forward);
    const long double boot=static_cast<long double>(calibration.at("host_timestamp_ns").get<uint64_t>())+
        calibration.at("host_to_boot_offset_ns").get<int64_t>()+delta*period;
    if(boot<0||boot>static_cast<long double>(INT64_MAX))CounterFail("gpu_clock_invalid","Calibrated endpoint cannot be mapped to BOOTTIME");
    return uint64_t(std::llround(boot));
}

template<class DeviceTable,class InstanceTable>
inline void CorrelateCounterWindow(const DeviceTable* table,const InstanceTable* instance,VkPhysicalDevice physical,
                                  VkDevice device,VkQueue queue,uint64_t queue_id,uint32_t family,
                                  uint32_t bits,double period,uint64_t start,uint64_t end,Json& sample){
    if(!counter_window_enabled)return;
    auto& window=report["counter_window"];
    try{
        if(!counter_resume_received)CounterFail("counter_handshake_unavailable",window.value("reason",std::string("Counter resume token was unavailable")));
        const auto completed=BootNs();
        if(!counter_entry_boot_ns)CounterFail("counter_clock_unavailable","Frame entry BOOTTIME was unavailable");
        Json driver;
        try{driver=CalibrateBoot(instance,table,physical,device);}catch(const std::exception& error){driver={{"status","unavailable"},{"reason",error.what()}};}
        window["clock_bridge"]["driver_calibration"]=driver;
        const auto bridge=QueueQueryBootBracket(table,device,queue,queue_id,family,bits,period,
            Clock::now()+std::chrono::nanoseconds(timeout_ns),driver);
        const auto mapped_start=TickToBoot(start,bits,period,bridge),mapped_end=TickToBoot(end,bits,period,bridge);
        const auto uncertainty=bridge.at("uncertainty_ns").template get<uint64_t>();
        if(mapped_end<mapped_start||(mapped_start<counter_entry_boot_ns&&counter_entry_boot_ns-mapped_start>uncertainty)||
           (mapped_end>completed&&mapped_end-completed>uncertainty))CounterFail("counter_clock_inconsistent","Mapped GPU timestamps exceed their observed host frame bounds");
        window["status"]="available";window["start_boot_ns"]=mapped_start;window["end_boot_ns"]=mapped_end;window["clock_bridge"]=bridge;window.erase("reason");
        window["scope"]="GPU timestamp endpoints from one ordinary frame replay; counter alignment is approximate within reported clock uncertainty";
        window["entry_boot_ns"]=counter_entry_boot_ns;window["completion_boot_ns"]=completed;
        sample["start_boot_ns"]=mapped_start;sample["end_boot_ns"]=mapped_end;sample["clock_calibration"]=bridge;
    }catch(const std::exception& error){window["status"]="unavailable";window["reason"]=error.what();window["start_boot_ns"]=nullptr;window["end_boot_ns"]=nullptr;sample.erase("start_boot_ns");sample.erase("end_boot_ns");sample.erase("clock_calibration");}
}
