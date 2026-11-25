#include "../../gfx.hpp"
#include "metal_internal.hpp"

namespace nw::gfx {

void record_metal_validation_error(MetalCore* core, const char* message) noexcept
{
    if (!core) {
        return;
    }
    const char* text = message ? message : "unknown Metal error";
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[MTL/ERROR] %s", text);
    std::lock_guard<std::mutex> lock(core->validation_report_mutex);
    ++core->validation_report.error_count;
    if (core->validation_report.first_error.empty()) {
        core->validation_report.first_error = text;
    }
}

void record_metal_validation_warning(MetalCore* core, const char* message) noexcept
{
    if (!core) {
        return;
    }
    const char* text = message ? message : "unknown Metal warning";
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[MTL/WARNING] %s", text);
    std::lock_guard<std::mutex> lock(core->validation_report_mutex);
    ++core->validation_report.warning_count;
    if (core->validation_report.first_warning.empty()) {
        core->validation_report.first_warning = text;
    }
}

Core* create_core(const CoreConfig& desc)
{
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "MTLCreateSystemDefaultDevice returned nil");
            return nullptr;
        }

        if (device.argumentBuffersSupport < MTLArgumentBuffersTier2) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "Metal device '%s' does not support required Tier 2 argument buffers",
                device.name.UTF8String);
            return nullptr;
        }

        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "Failed to create MTLCommandQueue for device '%s'",
                device.name.UTF8String);
            return nullptr;
        }

        auto* core = new MetalCore();
        core->validation_enabled = desc.enable_validation;
        core->device = device;
        core->command_queue = queue;
        core->counter_sampling_at_stage_boundary =
            [device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary];
        core->counter_sampling_at_draw_boundary =
            [device supportsCounterSampling:MTLCounterSamplingPointAtDrawBoundary];

        for (id<MTLCounterSet> counter_set in device.counterSets) {
            if ([counter_set.name isEqualToString:MTLCommonCounterSetTimestamp]) {
                core->timestamp_counter_set = counter_set;
                break;
            }
        }

        return as_gfx(core);
    }
}

void destroy_core(Core* core_ptr)
{
    @autoreleasepool {
        auto* core = as_metal(core_ptr);
        if (!core) {
            return;
        }
        GFX_CHECK(!core->context, "destroy_context must precede destroy_core");
        core->timestamp_counter_set = nil;
        core->command_queue = nil;
        core->device = nil;
        delete core;
    }
}

void reset_validation_report(Core* core_ptr)
{
    auto* core = as_metal(core_ptr);
    if (!core) {
        return;
    }
    std::lock_guard<std::mutex> lock(core->validation_report_mutex);
    core->validation_report = {};
}

ValidationReport validation_report(Core* core_ptr)
{
    auto* core = as_metal(core_ptr);
    if (!core) {
        return {};
    }
    std::lock_guard<std::mutex> lock(core->validation_report_mutex);
    return core->validation_report;
}

SDL_WindowFlags window_flags() noexcept
{
    return SDL_WINDOW_METAL;
}

} // namespace nw::gfx
