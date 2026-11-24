// Adapted from this repository's retained SDK-only loaded compatibility host:
// docs/validation/host-midi-loaded-compatibility-20261124.json
// reproduction.harness_source SHA-256:
// c07289b6cab8b7e772db92de8797e0ee4051a282acd4650feab4c482ecef1848
// The note/gain/sustain/recall timeline is retained; this revision adds CC123,
// pressure, explicit unity Expression, bounded raw capture and record identity.
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

// This host links SDK loader support only. Instrument implementations are
// obtained from the given bundle, never linked into this executable.
namespace {
namespace sdk = Steinberg;
namespace vst = Steinberg::Vst;
using Module = VST3::Hosting::Module;
using State = std::array<std::uint8_t, 16>;
constexpr int epoch_frames = 2048;
constexpr int tail_frames = 128;
constexpr vst::ParamID command_base = 0x1000;
constexpr std::array<const char*,3> processor_ids{
    "BDDA70F46F9244C1A5639EE371A0B804", "4C0E773A24FC4BE9B804BC71B8B98B33",
    "D73B886A72E04FC892435F1DC5F0B6A9"};
constexpr std::array<const char*,3> controller_ids{
    "EA801E8B97864ED9B3A0E6572D81CE61", "0569EDDBD21D4568A9ED52C3D8545B6D",
    "A45F23E1B68A4D79AF31472C8E909D52"};
static_assert(sizeof(float) == 4 && sizeof(double) == 8 &&
              std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559);
static_assert(std::endian::native == std::endian::little ||
              std::endian::native == std::endian::big);

void require(bool condition, const std::string& why) {
    if (!condition) { throw std::runtime_error(why); }
}
std::string hex(const State& state) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto byte : state) { out << std::setw(2) << static_cast<unsigned>(byte); }
    return out.str();
}
template<class Interface> State snapshot(Interface& object) {
    sdk::MemoryStream stream;
    require(object.getState(&stream) == sdk::kResultOk, "getState");
    require(stream.getSize() == 16, "16-byte state");
    State result{};
    std::copy_n(reinterpret_cast<const std::uint8_t*>(stream.getData()),16,result.begin());
    return result;
}

struct Instance {
    sdk::IPtr<vst::IComponent> component;
    sdk::IPtr<vst::IAudioProcessor> audio;
    sdk::IPtr<vst::IEditController> controller;
    bool component_initialized = false, controller_initialized = false;
    bool active = false, processing = false;
    std::string processor_id, controller_id;
    ~Instance() {
        if (processing) { (void)audio->setProcessing(false); }
        if (active) { (void)component->setActive(false); }
        if (controller_initialized) { (void)controller->terminate(); }
        if (component_initialized) { (void)component->terminate(); }
        controller = nullptr; audio = nullptr; component = nullptr;
    }
    void open(const Module& module,vst::HostApplication& host,int format,double rate) {
        require(module.getFactory().classCount() == 2,"two factory classes");
        for (const auto& info : module.getFactory().classInfos()) {
            if (info.category() == kVstAudioEffectClass) {
                require(!component,"single processor");
                processor_id = info.ID().toString();
                component = module.getFactory().createInstance<vst::IComponent>(info.ID());
            } else if (info.category() == kVstComponentControllerClass) {
                require(!controller,"single controller");
                controller_id = info.ID().toString();
                controller = module.getFactory().createInstance<vst::IEditController>(info.ID());
            } else { require(false,"factory category"); }
        }
        require(component && controller,"loaded component/controller");
        vst::IAudioProcessor* queried = nullptr;
        require(component->queryInterface(vst::IAudioProcessor::iid,
                    reinterpret_cast<void**>(&queried)) == sdk::kResultOk && queried,
                "audio processor interface");
        audio = sdk::owned(queried);
        require(component->initialize(&host) == sdk::kResultOk,"initialize component");
        component_initialized = true;
        require(controller->initialize(&host) == sdk::kResultOk,"initialize controller");
        controller_initialized = true;
        sdk::TUID advertised{};
        require(component->getControllerClassId(advertised) == sdk::kResultOk &&
                    VST3::UID{advertised}.toString() == controller_id,"advertised controller");
        require(component->getBusCount(vst::kAudio,vst::kInput) == 0 &&
                    component->getBusCount(vst::kAudio,vst::kOutput) == 1 &&
                    component->getBusCount(vst::kEvent,vst::kInput) == 1,"legacy bus layout");
        require(component->activateBus(vst::kAudio,vst::kOutput,0,true) == sdk::kResultOk,
                "activate output");
        require(component->activateBus(vst::kEvent,vst::kInput,0,true) == sdk::kResultOk,
                "activate event input");
        require(audio->canProcessSampleSize(format) == sdk::kResultOk,"sample format");
        vst::ProcessSetup setup{vst::kRealtime,format,epoch_frames,rate};
        require(audio->setupProcessing(setup) == sdk::kResultOk,"setupProcessing");
        require(component->setActive(true) == sdk::kResultOk,"setActive");
        active = true;
        require(audio->setProcessing(true) == sdk::kResultOk,"setProcessing");
        processing = true;
    }
    void recall(const State& state) {
        sdk::MemoryStream component_stream{const_cast<std::uint8_t*>(state.data()),16};
        sdk::MemoryStream controller_stream{const_cast<std::uint8_t*>(state.data()),16};
        require(component->setState(&component_stream) == sdk::kResultOk,"processor recall");
        require(controller->setComponentState(&controller_stream) == sdk::kResultOk,
                "controller recall");
        require(snapshot(*component) == state && snapshot(*controller) == state,
                "pending recall and controller bytes");
    }
};

struct Note { int frame; bool on; int channel; int pitch; int id; float velocity; };
// Shared keys, identified/anonymous retriggers, last-holder release,
// zero-velocity release, register extremes, and an ignored out-of-range pitch.
const std::vector<Note> notes{
    {32,true,0,28,10,.5F},{32,true,1,68,20,.75F},{32,true,15,100,30,.625F},
    {63,true,2,68,21,.375F},{127,true,1,68,20,.5F},{191,false,1,68,20,.2F},
    {256,false,2,68,21,.3F},{300,true,3,40,-1,.5F},{301,true,3,40,-1,.375F},
    {384,false,3,40,-1,.0F},{512,false,3,40,-1,.0F},{512,false,0,28,10,.0F},
    {512,true,0,28,11,.875F},{700,true,4,72,40,.0F},{768,false,15,100,30,.0F},
    {900,true,5,12,50,.75F},{901,false,5,12,50,.0F},
    {1024,true,4,72,40,.5F},{1152,true,4,72,40,.0F},
    {1280,false,0,28,11,.0F},{1400,true,2,91,60,.625F},{1600,false,2,91,60,.0F}
};
const std::vector<std::pair<int,double>> pedals{
    {0,0.0},{191,1.0},{512,0.0},{768,1.0},{1280,0.0},{1600,1.0},{1664,0.0}
};
struct Command { int frame; int channel; double value; };
const std::array<Command,7> commands{{
    {127,1,0.0},{256,2,1.0},{384,3,.5},{768,15,.25},
    {1152,4,1.0},{1400,2,0.0},{1664,0,1.0}
}};
struct Pressure { int frame; int channel; int pitch; int id; float value; };
const std::array<Pressure,8> pressures{{
    {0,0,0,-1,0.0F},{32,1,68,20,.25F},{63,2,68,21,.5F},
    {191,1,68,20,1.0F},{384,3,40,-1,.75F},
    {768,15,100,30,.0F},{1400,2,91,60,.625F},{2000,15,127,-1,1.0F}
}};

double dense_gain(int frame) {
    if (frame >= 1750 && frame < 1780) { return 0.0; }
    return static_cast<double>(32+(frame*13)%193)/256.0;
}
void point(vst::ParameterChanges& parameters,vst::ParamID id,int offset,double value) {
    sdk::int32 index = 0;
    auto* queue = parameters.addParameterData(id,index);
    require(queue && queue->addPoint(offset,value,index) == sdk::kResultOk,
            "prepare parameter point");
}
std::string ascii(const vst::String128 value) {
    std::string result;
    for (int i = 0;i < 128 && value[i];++i) {
        require(value[i] >= 32 && value[i] < 127 && value[i] != '"' && value[i] != '\\',
                "plain parameter label");
        result += static_cast<char>(value[i]);
    }
    return result;
}

template<class Sample> void run(const char* bundle,int rate,const std::string& partition,
                                const std::string& workload,const std::string& mode,
                                const char* output_path,const std::string& capture_id) {
    constexpr int format = std::is_same_v<Sample,float> ? vst::kSample32 : vst::kSample64;
    std::string error;
    auto module = Module::create(bundle,error);
    require(module != nullptr,"Module::create: "+error);
    vst::HostApplication host;
    auto instance = std::make_unique<Instance>();
    instance->open(*module,host,format,static_cast<double>(rate));
    const auto initial = snapshot(*instance->component);
    require(snapshot(*instance->controller) == initial,"default controller state");
    require(initial[0]=='R' && initial[1]=='H' && initial[2]=='S' && initial[3]=='T' &&
                initial[4]==1 && initial[5]==0,"RHST wire identity");
    const unsigned model = initial[6]+256U*initial[7];
    require(model >= 1 && model <= 3,"known state model");
    require(instance->processor_id == processor_ids[model-1] &&
            instance->controller_id == controller_ids[model-1],"frozen public class identities");
    State expected_initial{'R','H','S','T',1,0,static_cast<std::uint8_t>(model),0,
                           0,0,0,0x3f,0,0,0,0};
    require(initial == expected_initial,"exact legacy default state bytes");
    const bool explicit_unity = mode == "explicit1";
    const bool baseline = mode == "baseline";
    require(instance->controller->getParameterCount() == (baseline ? 18 : 24),
            "expected baseline/current public parameter count");
    if (!baseline) {
        vst::ParameterInfo expression{};
        require(instance->controller->getParameterInfo(2,expression) == sdk::kResultOk &&
                expression.id == 2 && expression.defaultNormalizedValue == 1.0 &&
                instance->controller->getParamNormalized(2) == 1.0,
                "current Expression identity and unity default");
    }
    const auto latency = instance->audio->getLatencySamples();
    require(model >= 1 && model <= 3 && latency == (model == 1 ? 0U : 48U),"model latency");
    require(instance->audio->getTailSamples() == vst::kInfiniteTail,"tail contract");
    std::ostringstream metadata;
    metadata << std::setprecision(17);
    metadata << "{\"capture_schema\":1,\"fixture_version\":1,\"complete\":true,"
             << "\"capture_id\":\"" << capture_id << "\",\"mode\":\"" << mode
             << "\",\"format_bits\":" << sizeof(Sample)*8 << ",\"sample_rate_hz\":" << rate
             << ",\"partition\":\"" << partition << "\",\"gain_workload\":\"" << workload
             << "\",\"byte_order\":\""
             << (std::endian::native == std::endian::little ? "little" : "big")
             << "\",\"processor_id\":\"" << instance->processor_id
             << "\",\"controller_id\":\"" << instance->controller_id
             << "\",\"model\":" << model << ",\"latency\":" << latency
             << ",\"tail_samples\":" << instance->audio->getTailSamples()
             << ",\"parameter_count\":" << instance->controller->getParameterCount()
             << ",\"legacy_parameters\":[";
    for (int legacy_index = 0;legacy_index < 18;++legacy_index) {
        const int index = legacy_index < 2 || baseline ? legacy_index : legacy_index+1;
        const auto id = legacy_index < 2 ? static_cast<vst::ParamID>(legacy_index) :
            command_base+static_cast<vst::ParamID>(legacy_index-2);
        vst::ParameterInfo info{};
        require(instance->controller->getParameterInfo(index,info) == sdk::kResultOk &&
                    info.id == static_cast<vst::ParamID>(id),"legacy parameter identity");
        if (legacy_index) { metadata << ','; }
        metadata << "{\"id\":" << info.id << ",\"title\":\"" << ascii(info.title)
                 << "\",\"units\":\"" << ascii(info.units) << "\",\"flags\":" << info.flags
                 << ",\"step_count\":" << info.stepCount
                 << ",\"default_normalized\":" << info.defaultNormalizedValue << '}';
    }
    metadata << "],\"initial_state\":\"" << hex(initial) << "\",\"checkpoints\":[";
    bool first = true;
    int callbacks = 0,nonzero_samples = 0;
    int note_events = 0,pressure_events = 0,command_points = 0,expression_points = 0;
    std::array<int,2> epoch_nonzero{};
    std::array<double,2> epoch_peaks{};
    std::vector<Sample> trace;
    trace.reserve(static_cast<std::size_t>((2*epoch_frames+tail_frames)*2));
    auto checkpoint = [&](const std::string& name,int at,int count,std::uint64_t flags) {
        if (!first) { metadata << ','; }
        first = false;
        metadata << "{\"name\":\"" << name << "\",\"frame\":" << at
                 << ",\"frames\":" << count << ",\"silence_flags\":" << flags
                 << ",\"state\":\"" << hex(snapshot(*instance->component)) << "\"}";
    };
    auto flush = [&](int at,double gain,double pedal) {
        vst::ParameterChanges parameters{19};
        point(parameters,0,0,gain); point(parameters,1,0,pedal);
        point(parameters,command_base+7,0,.5); ++command_points;
        if (explicit_unity) { point(parameters,2,0,1.0); ++expression_points; }
        vst::ProcessData data{};
        data.processMode = vst::kRealtime;
        data.symbolicSampleSize = format;
        data.inputParameterChanges = &parameters;
        require(instance->audio->process(data) == sdk::kResultOk,"zero-frame legacy flush");
        ++callbacks;
        checkpoint("flush",at,0,0);
    };
    flush(0,.25,1.0);
    instance->recall(initial);
    checkpoint("pending_default_recall",0,0,0);
    std::vector<int> sizes;
    if (partition == "whole") { sizes = {epoch_frames}; }
    else if (partition == "fixed64") { sizes = {64}; }
    else if (partition == "ragged") { sizes = {1,7,31,128,3,257,64,509,2,100}; }
    else { throw std::runtime_error("unknown partition"); }
    auto render = [&](int epoch,int start,int count,bool silence_only) {
        std::vector<Sample> left(static_cast<std::size_t>(count)+1,Sample{123});
        std::vector<Sample> right(static_cast<std::size_t>(count)+1,Sample{456});
        std::array<Sample*,2> channels{left.data(),right.data()};
        vst::AudioBusBuffers bus{};
        bus.numChannels = 2; bus.silenceFlags = 255;
        if constexpr (std::is_same_v<Sample,float>) { bus.channelBuffers32 = channels.data(); }
        else { bus.channelBuffers64 = channels.data(); }
        vst::EventList events{64};
        vst::ParameterChanges parameters{19};
        if (!silence_only) {
            for (const auto& note : notes) {
                if (note.frame < start || note.frame >= start+count) { continue; }
                vst::Event event{};
                event.sampleOffset = note.frame-start;
                event.ppqPosition = static_cast<double>(epoch*epoch_frames+note.frame)/rate;
                if (note.on) {
                    event.type = vst::Event::kNoteOnEvent;
                    event.noteOn.channel = static_cast<sdk::int16>(note.channel);
                    event.noteOn.pitch = static_cast<sdk::int16>(note.pitch);
                    event.noteOn.velocity = note.velocity; event.noteOn.noteId = note.id;
                } else {
                    event.type = vst::Event::kNoteOffEvent;
                    event.noteOff.channel = static_cast<sdk::int16>(note.channel);
                    event.noteOff.pitch = static_cast<sdk::int16>(note.pitch);
                    event.noteOff.velocity = note.velocity; event.noteOff.noteId = note.id;
                }
                require(events.addEvent(event) == sdk::kResultOk,"prepare note");
                ++note_events;
            }
            for (const auto& pressure : pressures) {
                if (pressure.frame < start || pressure.frame >= start+count) { continue; }
                vst::Event event{};
                event.type = vst::Event::kPolyPressureEvent;
                event.sampleOffset = pressure.frame-start;
                event.polyPressure.channel = static_cast<sdk::int16>(pressure.channel);
                event.polyPressure.pitch = static_cast<sdk::int16>(pressure.pitch);
                event.polyPressure.noteId = pressure.id;
                event.polyPressure.pressure = pressure.value;
                require(events.addEvent(event) == sdk::kResultOk,"prepare pressure");
                ++pressure_events;
            }
            // Sorting by offsets is required by the plug-in's public event contract.
            // EventList preserves insertion order, so rebuild a stable sorted list.
            std::vector<vst::Event> ordered;
            for (sdk::int32 i = 0;i < events.getEventCount();++i) {
                vst::Event event{};
                require(events.getEvent(i,event) == sdk::kResultOk,"read prepared event");
                ordered.push_back(event);
            }
            std::stable_sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b) {
                return a.sampleOffset < b.sampleOffset;
            });
            events.clear();
            for (auto& event : ordered) {
                require(events.addEvent(event) == sdk::kResultOk,"sort prepared events");
            }
            for (const auto& command : commands) {
                if (command.frame >= start && command.frame < start+count) {
                    point(parameters,command_base+static_cast<vst::ParamID>(command.channel),
                          command.frame-start,command.value);
                    ++command_points;
                }
            }
            for (int channel = 0;channel < 16;++channel) {
                const int at = 1880+channel;
                if (at >= start && at < start+count) {
                    point(parameters,command_base+static_cast<vst::ParamID>(channel),at-start,.25);
                    ++command_points;
                }
            }
            for (const auto& [at,value] : pedals) {
                if (at >= start && at < start+count) { point(parameters,1,at-start,value); }
            }
            if (workload == "dense") {
                for (int offset = 0;offset < count;++offset) {
                    point(parameters,0,offset,dense_gain(start+offset));
                }
            } else if (workload == "sparse") {
                // Per-callback endpoints: sparse audio is compared only between
                // baseline/current runs having exactly the same partition.
                point(parameters,0,0,dense_gain(start));
                if (count > 2) { point(parameters,0,count/2,dense_gain(start+count/2)); }
                if (count > 1) { point(parameters,0,count-1,dense_gain(start+count-1)); }
            } else { throw std::runtime_error("unknown workload"); }
        }
        if (explicit_unity) {
            point(parameters,2,0,1.0); ++expression_points;
            if (count > 1) { point(parameters,2,count-1,1.0); ++expression_points; }
        }
        vst::ProcessData data{};
        data.processMode = vst::kRealtime; data.symbolicSampleSize = format;
        data.numSamples = count; data.numOutputs = 1; data.outputs = &bus;
        data.inputEvents = &events; data.inputParameterChanges = &parameters;
        require(instance->audio->process(data) == sdk::kResultOk,"loaded process callback");
        ++callbacks;
        bool any = false;
        for (int i = 0;i < count;++i) {
            const auto index = static_cast<std::size_t>(i);
            require(std::isfinite(left[index]) && std::isfinite(right[index]),"finite stereo");
            require(std::bit_cast<std::array<std::byte,sizeof(Sample)>>(left[index]) ==
                    std::bit_cast<std::array<std::byte,sizeof(Sample)>>(right[index]),
                    "bit-identical dual-mono stereo");
            any = any || left[index] != 0;
            nonzero_samples += left[index] != 0;
            if (!silence_only) {
                epoch_nonzero[static_cast<std::size_t>(epoch)] += left[index] != 0;
                epoch_peaks[static_cast<std::size_t>(epoch)] = std::max(
                    epoch_peaks[static_cast<std::size_t>(epoch)],std::abs(static_cast<double>(left[index])));
            }
            trace.push_back(left[index]); trace.push_back(right[index]);
        }
        require(left[static_cast<std::size_t>(count)] == Sample{123} &&
                right[static_cast<std::size_t>(count)] == Sample{456},"output bounds canaries");
        require(bus.silenceFlags == (any ? 0U : 3U),"silence flags");
        require(!silence_only || !any,"recall clears mechanics and filter history");
        checkpoint(silence_only ? "reset_silence" : "render",epoch*epoch_frames+start,count,
                   bus.silenceFlags);
    };
    for (int epoch = 0;epoch < 2;++epoch) {
        if (epoch == 1) {
            State recalled = initial;
            recalled[8]=0; recalled[9]=0; recalled[10]=0x80; recalled[11]=0x3e;
            recalled[12]=0; recalled[13]=0; recalled[14]=0x80; recalled[15]=0x3f;
            instance->recall(recalled);
            checkpoint("pending_custom_recall",epoch_frames,0,0);
            flush(epoch_frames,.75,0.0);
        }
        std::size_t block = 0;
        for (int start = 0;start < epoch_frames;) {
            const int count = std::min(sizes[block++%sizes.size()],epoch_frames-start);
            render(epoch,start,count,false); start += count;
        }
    }
    for (std::size_t epoch = 0;epoch < epoch_nonzero.size();++epoch) {
        require(epoch_nonzero[epoch] >= 128 && epoch_peaks[epoch] > 1e-6,
                "nonvacuous nonzero musical output in each epoch");
    }
    require(note_events == 44 && pressure_events == 16 && command_points == 48,
            "complete bounded legacy event and command timeline");
    const auto live = snapshot(*instance->component);
    instance->recall(live);
    checkpoint("saved_live_round_trip",2*epoch_frames,0,0);
    instance->recall(initial);
    checkpoint("final_pending_default_recall",2*epoch_frames,0,0);
    render(2,0,tail_frames,true);
    require(snapshot(*instance->component) == initial,"default state after reset silence");
    require(instance->audio->getLatencySamples() == latency,"latency stable");
    require(trace.size() == static_cast<std::size_t>((2*epoch_frames+tail_frames)*2),
            "complete fixed-size trace");
    require(!std::filesystem::exists(output_path),"refuse existing trace output");
    std::ofstream binary(output_path,std::ios::binary);
    require(binary.good(),"open trace output");
    binary.write(reinterpret_cast<const char*>(trace.data()),
                 static_cast<std::streamsize>(trace.size()*sizeof(Sample)));
    require(binary.good(),"write trace output");
    binary.close();
    require(!binary.fail(),"close trace output");
    metadata << "],\"callbacks\":" << callbacks << ",\"nonzero_left_samples\":"
             << nonzero_samples << ",\"output_frames\":" << trace.size()/2
             << ",\"note_events\":" << note_events << ",\"pressure_events\":" << pressure_events
             << ",\"command_points\":" << command_points
             << ",\"expression_points\":" << expression_points
             << ",\"epoch_nonzero_left_samples\":[" << epoch_nonzero[0] << ',' << epoch_nonzero[1]
             << "],\"epoch_peak_abs\":[" << epoch_peaks[0] << ',' << epoch_peaks[1] << "]}";
    std::cout << metadata.str() << '\n';
}
} // namespace

int main(int argc,char** argv) {
    try {
        require(argc == 9,"usage: rhodes_vst3_legacy_capture BUNDLE FORMAT RATE PARTITION WORKLOAD MODE OUTPUT CAPTURE_ID");
        require(std::string{argv[2]} == "32" || std::string{argv[2]} == "64","strict sample format");
        require(std::string{argv[3]} == "48000" || std::string{argv[3]} == "192000","bounded sample rates");
        const std::string mode{argv[6]},capture_id{argv[8]};
        require(mode == "baseline" || mode == "default" || mode == "explicit1","capture mode");
        require(capture_id.size() == 32 && std::all_of(capture_id.begin(),capture_id.end(),[](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }),"32-character hexadecimal capture ID");
        require(!std::filesystem::exists(argv[7]),"refuse existing capture output");
        const int format = std::stoi(argv[2]),rate = std::stoi(argv[3]);
        require(format == 32 || format == 64,"format must be 32 or 64");
        if (format == 32) { run<float>(argv[1],rate,argv[4],argv[5],mode,argv[7],capture_id); }
        else { run<double>(argv[1],rate,argv[4],argv[5],mode,argv[7],capture_id); }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL loaded compatibility: " << error.what() << '\n';
        return 1;
    }
}
