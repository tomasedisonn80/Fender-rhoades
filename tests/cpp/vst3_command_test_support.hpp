#pragma once

#include "tests/cpp/vst3_test_support.hpp"

namespace rhodes::vst3::test_host {

// Host-owned command queues use the specified raw command budget, independently
// of the processor's internal action expansion. All storage is prepared before
// process(); access counters make bounded traversal observable to tests.
struct CommandQueue final : FixedHostObject<vst::IParamValueQueue> {
    std::array<ParameterQueue::Point, 256> values{};
    vst::ParamID id = kAllNotesOffBaseId;
    sdk::int32 count = 0;
    sdk::int32 failed_index = -1;
    std::size_t reads = 0;

    vst::ParamID PLUGIN_API getParameterId() override { return id; }
    sdk::int32 PLUGIN_API getPointCount() override { return count; }
    sdk::tresult PLUGIN_API getPoint(sdk::int32 index, sdk::int32& offset,
                                    vst::ParamValue& value) override {
        ++reads;
        if (index < 0 || index >= count || index == failed_index ||
            static_cast<std::size_t>(index) >= values.size()) { return sdk::kResultFalse; }
        offset = values[static_cast<std::size_t>(index)].offset;
        value = values[static_cast<std::size_t>(index)].value;
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API addPoint(sdk::int32, vst::ParamValue, sdk::int32&) override {
        return sdk::kNotImplemented;
    }
};

struct CommandChanges final : FixedHostObject<vst::IParameterChanges> {
    // Legacy gain/sustain plus sixteen commands; Expression remains at unity.
    // This fixture capacity is independent of the nineteen-queue public limit.
    std::array<vst::IParamValueQueue*, 18> values{};
    sdk::int32 count = 0;
    std::size_t reads = 0;
    sdk::int32 PLUGIN_API getParameterCount() override { return count; }
    vst::IParamValueQueue* PLUGIN_API getParameterData(sdk::int32 index) override {
        ++reads;
        return index >= 0 && index < count && static_cast<std::size_t>(index) < values.size() ?
            values[static_cast<std::size_t>(index)] : nullptr;
    }
    vst::IParamValueQueue* PLUGIN_API addParameterData(const vst::ParamID&,
                                                     sdk::int32&) override { return nullptr; }
};

} // namespace rhodes::vst3::test_host
