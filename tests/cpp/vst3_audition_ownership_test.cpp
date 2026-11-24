#include "plugins/vst3/controller.hpp"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#if defined(__SSE__)
#include <xmmintrin.h>
#endif

namespace rhodes::vst3 {
struct AuditionOwnershipTestAccess {
    static void exhaust_after_two_registrations(RhodesController& controller) {
        controller.next_audition_owner_ = std::numeric_limits<std::uint64_t>::max() - 1;
    }
};
} // namespace rhodes::vst3

// The fixture records the actual SDK IMessage traffic. Its message IDs,
// attribute names, keys and expected order are literal public-wire oracles;
// no processor helper or production queue builds the expected sequence.
// This is a controller test, not an attached-window or audio rendering test.
namespace {
namespace sdk = Steinberg;
namespace vst = Steinberg::Vst;
using Controller = rhodes::vst3::RhodesController;
using Owner = Controller::AuditionOwner;
std::size_t checks = 0;

void check(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

template <class Interface>
struct FixedObject : Interface {
    sdk::uint32 PLUGIN_API addRef() override { return 1; }
    sdk::uint32 PLUGIN_API release() override { return 1; }
    sdk::tresult PLUGIN_API queryInterface(const sdk::TUID interface_id, void** object) override {
        if (interface_id == nullptr || object == nullptr) { return sdk::kInvalidArgument; }
        *object = nullptr;
        if (sdk::FUnknownPrivate::iidEqual(interface_id, Interface::iid) ||
            sdk::FUnknownPrivate::iidEqual(interface_id, sdk::FUnknown::iid)) {
            *object = static_cast<Interface*>(this);
            return sdk::kResultOk;
        }
        return sdk::kNoInterface;
    }
};

struct Attributes final : FixedObject<vst::IAttributeList> {
    sdk::int64 key = -1;
    sdk::int64 down = -1;
    double velocity = -1.0;
    const char* fail_field = nullptr;

    sdk::tresult PLUGIN_API setInt(AttrID id, sdk::int64 value) override {
        if (fail_field != nullptr && std::strcmp(id, fail_field) == 0) {
            return sdk::kResultFalse;
        }
        if (std::strcmp(id, "key") == 0) { key = value; return sdk::kResultOk; }
        if (std::strcmp(id, "down") == 0) { down = value; return sdk::kResultOk; }
        return sdk::kResultFalse;
    }
    sdk::tresult PLUGIN_API getInt(AttrID id, sdk::int64& value) override {
        if (std::strcmp(id, "key") == 0) { value = key; return sdk::kResultOk; }
        if (std::strcmp(id, "down") == 0) { value = down; return sdk::kResultOk; }
        return sdk::kResultFalse;
    }
    sdk::tresult PLUGIN_API setFloat(AttrID id, double value) override {
        if (std::strcmp(id, "velocity") != 0 ||
            (fail_field != nullptr && std::strcmp(id, fail_field) == 0)) {
            return sdk::kResultFalse;
        }
        velocity = value;
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API getFloat(AttrID id, double& value) override {
        if (std::strcmp(id, "velocity") != 0) { return sdk::kResultFalse; }
        value = velocity;
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API setString(AttrID, const vst::TChar*) override {
        return sdk::kNotImplemented;
    }
    sdk::tresult PLUGIN_API getString(AttrID, vst::TChar*, sdk::uint32) override {
        return sdk::kNotImplemented;
    }
    sdk::tresult PLUGIN_API setBinary(AttrID, const void*, sdk::uint32) override {
        return sdk::kNotImplemented;
    }
    sdk::tresult PLUGIN_API getBinary(AttrID, const void*&, sdk::uint32&) override {
        return sdk::kNotImplemented;
    }
};

struct Message final : FixedObject<vst::IMessage> {
    const char* id = nullptr;
    Attributes attributes{};
    bool has_attributes = true;
    sdk::FIDString PLUGIN_API getMessageID() override { return id; }
    void PLUGIN_API setMessageID(sdk::FIDString value) override { id = value; }
    vst::IAttributeList* PLUGIN_API getAttributes() override {
        return has_attributes ? &attributes : nullptr;
    }
};

struct Application final : FixedObject<vst::IHostApplication> {
    Message message;
    std::size_t fail_allocations = 0;
    bool omit_next_attributes = false;
    const char* fail_next_field = nullptr;

    sdk::tresult PLUGIN_API getName(vst::String128 name) override {
        name[0] = 'T';
        name[1] = 0;
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API createInstance(sdk::TUID cid, sdk::TUID interface_id,
                                          void** object) override {
        if (object == nullptr) { return sdk::kInvalidArgument; }
        *object = nullptr;
        if (!sdk::FUnknownPrivate::iidEqual(cid, vst::IMessage::iid) ||
            !sdk::FUnknownPrivate::iidEqual(interface_id, vst::IMessage::iid)) {
            return sdk::kNoInterface;
        }
        if (fail_allocations != 0) { --fail_allocations; return sdk::kResultFalse; }
        message.id = nullptr;
        message.attributes.key = -1;
        message.attributes.down = -1;
        message.attributes.velocity = -1.0;
        message.attributes.fail_field = fail_next_field;
        fail_next_field = nullptr;
        message.has_attributes = !omit_next_attributes;
        omit_next_attributes = false;
        *object = static_cast<vst::IMessage*>(&message);
        return sdk::kResultOk;
    }
};

struct Recorder final : FixedObject<vst::IConnectionPoint> {
    enum class Kind { note, release };
    struct Record {
        Kind kind = Kind::note;
        sdk::int64 key = -1;
        sdk::int64 down = -1;
        double velocity = -1.0;
        bool accepted = false;
    };
    std::array<Record, 128> records{};
    std::size_t count = 0;
    std::size_t fail_notes = 0;
    std::size_t fail_releases = 0;
    bool malformed = false;

    sdk::tresult PLUGIN_API connect(vst::IConnectionPoint*) override { return sdk::kResultOk; }
    sdk::tresult PLUGIN_API disconnect(vst::IConnectionPoint*) override { return sdk::kResultOk; }
    sdk::tresult PLUGIN_API notify(vst::IMessage* message) override {
        if (message == nullptr || message->getMessageID() == nullptr || count == records.size()) {
            malformed = true;
            return sdk::kResultFalse;
        }
        Record record;
        if (std::strcmp(message->getMessageID(), "RhodesPhotoNoteV1") == 0) {
            auto* attributes = message->getAttributes();
            if (attributes == nullptr ||
                attributes->getInt("key", record.key) != sdk::kResultOk ||
                attributes->getInt("down", record.down) != sdk::kResultOk ||
                attributes->getFloat("velocity", record.velocity) != sdk::kResultOk) {
                malformed = true;
                return sdk::kResultFalse;
            }
            record.accepted = fail_notes == 0;
            if (fail_notes != 0) { --fail_notes; }
        } else if (std::strcmp(message->getMessageID(), "RhodesPhotoReleaseV1") == 0) {
            record.kind = Kind::release;
            record.accepted = fail_releases == 0;
            if (fail_releases != 0) { --fail_releases; }
        } else {
            malformed = true;
            return sdk::kResultFalse;
        }
        records[count++] = record;
        return record.accepted ? sdk::kResultOk : sdk::kResultFalse;
    }

    void note(std::size_t index, int key, bool down, double velocity, bool accepted = true) const {
        check(!malformed && index < count, "missing or malformed note message");
        const auto& value = records[index];
        check(value.kind == Kind::note && value.key == key && value.down == (down ? 1 : 0) &&
              value.velocity == velocity && value.accepted == accepted,
              "note message differs from literal wire sequence");
    }
    void release(std::size_t index, bool accepted = true) const {
        check(!malformed && index < count, "missing or malformed emergency message");
        check(records[index].kind == Kind::release && records[index].accepted == accepted,
              "emergency message differs from literal wire sequence");
    }
};

struct Fixture {
    Application application;
    Recorder recorder;
    Controller controller;
    bool initialized = false;

    Fixture() {
        check(controller.registerAuditionOwner() == 0, "uninitialized owner registration accepted");
        check(controller.initialize(&application) == sdk::kResultOk, "controller initialization");
        initialized = true;
        check(controller.connect(&recorder) == sdk::kResultOk, "SDK connection");
    }
    ~Fixture() { if (initialized) { (void)controller.terminate(); } }
    Owner owner() {
        const auto value = controller.registerAuditionOwner();
        check(value != 0, "owner allocation");
        return value;
    }
    void down(Owner owner, int key, float velocity = 0.75F) {
        check(controller.auditionNote(owner, key, true, velocity), "accepted owned key down");
    }
    void up(Owner owner, int key) {
        check(controller.auditionNote(owner, key, false, 0.0F), "accepted owned key up");
    }
    void close(Owner owner) {
        check(controller.unregisterAuditionOwner(owner), "owned cleanup");
    }
};

void fresh_down_and_idle_close() {
    Fixture f;
    const auto active = f.owner();
    const auto idle = f.owner();
    f.up(active, 40); // A fresh mouse-down first cancels any previous local note.
    check(f.recorder.count == 0, "idle cancellation sent a message");
    f.down(active, 40);
    check(f.recorder.count == 1, "fresh down sent an extra/global release");
    f.recorder.note(0, 40, true, 0.75);
    check(f.controller.retryAuditionRelease(), "idle service failed");
    f.close(idle);
    f.close(idle);
    check(f.recorder.count == 1 && f.controller.heldAuditionKey(active) == 40,
          "idle sibling close or idle service disturbed the held view");
    f.up(active, 40);
    f.recorder.note(1, 40, false, 0.0);
    check(f.recorder.count == 2 && f.controller.heldAuditionKey(active) == -1,
          "normal up did not finish with exactly one ordered release");
}

void transitions_are_ordered() {
    Fixture f;
    const auto owner = f.owner();
    f.down(owner, 40);
    f.up(owner, 40);
    f.down(owner, 41);
    f.down(owner, 42); // Direct callers also get cleanup of the previous key.
    f.down(owner, 42, 0.5F); // A new strike keeps the same owner without an up.
    check(f.recorder.count == 6 && f.controller.heldAuditionKey(owner) == 42,
          "transitions lost the current key or emitted a global release");
    f.recorder.note(0, 40, true, 0.75);
    f.recorder.note(1, 40, false, 0.0);
    f.recorder.note(2, 41, true, 0.75);
    f.recorder.note(3, 41, false, 0.0);
    f.recorder.note(4, 42, true, 0.75);
    f.recorder.note(5, 42, true, 0.5);
    f.close(owner);
    f.recorder.note(6, 42, false, 0.0);
    check(f.recorder.count == 7, "close sent more than its final key up");
}

void shared_and_distinct_keys() {
    {
        Fixture f;
        const auto a = f.owner();
        const auto b = f.owner();
        f.down(a, 40, 0.25F);
        f.down(b, 40);
        f.up(a, 40);
        f.up(a, 40);
        f.close(a);
        check(f.recorder.count == 2 && f.controller.heldAuditionKey(b) == 40,
              "first shared-key release cancelled the other owner");
        f.recorder.note(0, 40, true, 0.25);
        f.recorder.note(1, 40, true, 0.75);
        f.close(b);
        f.recorder.note(2, 40, false, 0.0);
        check(f.recorder.count == 3, "last shared-key owner did not emit exactly one up");
    }
    {
        Fixture f;
        const auto a = f.owner();
        const auto b = f.owner();
        f.down(a, 40);
        f.down(b, 41);
        f.close(a);
        f.recorder.note(2, 40, false, 0.0);
        check(f.recorder.count == 3 && f.controller.heldAuditionKey(b) == 41,
              "closing one key cancelled a distinct sibling key");
        f.close(b);
        f.recorder.note(3, 41, false, 0.0);
        check(f.recorder.count == 4, "distinct-key close emitted extra traffic");
    }
}

void stale_tokens_and_invalid_requests() {
    Fixture f;
    const auto stale = f.owner();
    f.down(stale, 40);
    f.close(stale);
    const auto live = f.owner();
    check(live != stale, "closed token reused by a new view");
    f.down(live, 41);
    check(!f.controller.auditionNote(stale, 41, false, 0.0F), "stale owner accepted");
    f.close(stale);
    for (const int key : {-1, 73}) {
        check(!f.controller.auditionNote(live, key, true, 0.75F), "invalid key accepted");
    }
    for (const float velocity : {-1.0F, 2.0F, std::numeric_limits<float>::infinity(),
                                 std::numeric_limits<float>::quiet_NaN()}) {
        check(!f.controller.auditionNote(live, 41, true, velocity), "invalid velocity accepted");
    }
    check(!f.controller.auditionNote(0, 41, false, 0.0F), "zero owner accepted");
    check(f.recorder.count == 3 && f.controller.heldAuditionKey(live) == 41,
          "stale/invalid request disturbed a live owner");
    f.close(live);
    f.recorder.note(3, 41, false, 0.0);
}

void zero_velocity_releases_only_its_owner() {
    for (const float zero : {0.0F, std::bit_cast<float>(std::uint32_t{0x80000000U})}) {
        Fixture f;
        const auto a = f.owner();
        const auto b = f.owner();
        f.down(a, 40);
        check(f.controller.auditionNote(b, 40, true, zero), "idle note-on zero rejected");
        check(f.recorder.count == 1 && f.controller.heldAuditionKey(a) == 40 &&
              f.controller.heldAuditionKey(b) == -1, "idle note-on zero released another owner");
        f.down(b, 40);
        check(f.controller.auditionNote(b, 40, true, zero), "shared note-on zero rejected");
        check(f.recorder.count == 2 && f.controller.heldAuditionKey(a) == 40 &&
              f.controller.heldAuditionKey(b) == -1, "shared note-on zero ignored last-owner rule");
        check(f.controller.auditionNote(a, 40, true, zero), "last-owner note-on zero rejected");
        f.recorder.note(2, 40, false, 0.0);
        check(f.recorder.count == 3 && f.controller.heldAuditionKey(a) == -1,
              "last note-on zero retained physical or controller ownership");
    }
}

void positive_subnormal_stays_a_down() {
    Fixture f;
    const auto owner = f.owner();
#if defined(__SSE__)
    const auto saved_mxcsr = _mm_getcsr();
    _mm_setcsr(saved_mxcsr | (1U << 6U) | (1U << 15U)); // DAZ and FTZ.
#endif
    const bool accepted = f.controller.auditionNote(
        owner, 40, true, std::bit_cast<float>(std::uint32_t{1}));
#if defined(__SSE__)
    _mm_setcsr(saved_mxcsr);
#endif
    check(accepted && f.controller.heldAuditionKey(owner) == 40,
          "positive subnormal did not retain its down ownership");
    f.recorder.note(0, 40, true, 0x1p-149);
    f.close(owner);
    f.recorder.note(1, 40, false, 0.0);
}

void bounded_registration_and_wrap() {
    {
        Fixture f;
        std::array<Owner, 32> owners{};
        for (auto& owner : owners) { owner = f.owner(); }
        f.down(owners.back(), 40);
        check(f.controller.registerAuditionOwner() == 0, "bounded owner registry exceeded 32");
        check(f.recorder.count == 1 && f.controller.heldAuditionKey(owners.back()) == 40,
              "registry exhaustion disturbed held cleanup");
        f.close(owners.front());
        const auto replacement = f.owner();
        check(replacement != owners.front(), "recycled slot reused stale token");
        for (const auto owner : owners) { f.close(owner); }
        f.close(replacement);
        check(f.recorder.count == 2, "bounded registry lost or duplicated held cleanup");
        f.recorder.note(1, 40, false, 0.0);
    }
    {
        Fixture f;
        rhodes::vst3::AuditionOwnershipTestAccess::exhaust_after_two_registrations(f.controller);
        const auto penultimate = f.owner();
        const auto last = f.owner();
        check(penultimate == std::numeric_limits<Owner>::max() - 1 &&
              last == std::numeric_limits<Owner>::max(), "token exhaustion fixture boundary");
        check(f.controller.registerAuditionOwner() == 0, "token wrap did not fail closed");
        f.down(last, 40);
        f.close(penultimate);
        check(f.controller.registerAuditionOwner() == 0, "free slot revived wrapped tokens");
        f.close(last);
        f.recorder.note(1, 40, false, 0.0);
        check(f.recorder.count == 2, "token exhaustion lost held cleanup");
    }
}

void failed_transport_cancels_all_owners() {
    for (const int failure : {0, 1, 2, 3, 4, 5}) {
        Fixture f;
        const auto a = f.owner();
        const auto b = f.owner();
        f.down(a, 40);
        f.down(b, 41);
        if (failure == 0) { f.application.fail_allocations = 1; }
        else if (failure == 1) { f.application.omit_next_attributes = true; }
        else if (failure == 2) { f.application.fail_next_field = "key"; }
        else if (failure == 3) { f.application.fail_next_field = "down"; }
        else if (failure == 4) { f.application.fail_next_field = "velocity"; }
        else { f.recorder.fail_notes = 1; }
        check(!f.controller.auditionNote(a, 40, false, 0.0F), "failed release reported success");
        check(f.controller.heldAuditionKey(a) == -1 && f.controller.heldAuditionKey(b) == -1,
              "emergency left another view's held-key feedback stale");
        const std::size_t emergency_index = failure == 5 ? 3 : 2;
        if (failure == 5) { f.recorder.note(2, 40, false, 0.0, false); }
        f.recorder.release(emergency_index);
        f.close(a);
        f.close(b);
        check(f.controller.retryAuditionRelease(), "successful emergency remained pending");
        check(f.recorder.count == emergency_index + 1, "post-emergency close emitted extra traffic");
    }
    {
        Fixture f;
        const auto a = f.owner();
        const auto b = f.owner();
        f.down(a, 40);
        f.recorder.fail_notes = 1;
        check(!f.controller.auditionNote(b, 41, true, 0.75F), "failed down reported success");
        f.recorder.note(1, 41, true, 0.75, false);
        f.recorder.release(2);
        check(f.controller.heldAuditionKey(a) == -1 && f.controller.heldAuditionKey(b) == -1,
              "failed down retained a UI owner");
    }
}

void pending_emergency_retries_without_fresh_down() {
    Fixture f;
    const auto a = f.owner();
    const auto b = f.owner();
    f.down(a, 40);
    f.down(b, 41);
    f.recorder.fail_notes = 1;
    f.recorder.fail_releases = 2;
    check(!f.controller.auditionNote(a, 40, false, 0.0F), "failed up reported success");
    f.recorder.note(2, 40, false, 0.0, false);
    f.recorder.release(3, false);
    check(!f.controller.auditionNote(b, 42, true, 0.75F), "down bypassed failed emergency");
    f.recorder.release(4, false);
    check(!f.controller.auditionNote(b, 42, true, 0.75F), "recovery call also queued a fresh down");
    f.recorder.release(5);
    check(f.recorder.count == 6 && f.controller.heldAuditionKey(b) == -1,
          "pending emergency accepted fresh UI ownership");
    check(f.controller.retryAuditionRelease(), "completed emergency could not become idle");
    check(f.recorder.count == 6, "idle retry sent an unnecessary global release");
}

void failed_final_close_retains_emergency() {
    Fixture f;
    const auto owner = f.owner();
    f.down(owner, 40);
    f.application.fail_allocations = 2; // Both the ordered up and immediate panic fail.
    check(!f.controller.unregisterAuditionOwner(owner), "failed close reported success");
    check(f.controller.heldAuditionKey(owner) == -1 && f.recorder.count == 1,
          "closed owner survived failed cleanup");
    check(f.controller.retryAuditionRelease(), "failed final close forgot pending cleanup");
    f.recorder.release(1);
    check(f.recorder.count == 2, "final-close emergency was not retried exactly once");
}

void termination_releases_owned_keys() {
    Fixture f;
    const auto a = f.owner();
    const auto b = f.owner();
    (void)f.owner(); // An idle view must not append a global release at termination.
    f.down(a, 40);
    f.down(b, 40);
    check(f.controller.terminate() == sdk::kResultOk, "terminate held controller");
    f.initialized = false;
    f.recorder.note(2, 40, false, 0.0);
    check(f.recorder.count == 3 && f.controller.heldAuditionKey(a) == -1 &&
          f.controller.heldAuditionKey(b) == -1, "termination lost shared held-key cleanup");
    check(f.controller.registerAuditionOwner() == 0, "terminated registration accepted");
    check(!f.controller.auditionNote(b, 40, true, 0.75F), "terminated down accepted");
    check(f.controller.initialize(&f.application) == sdk::kResultOk, "controller reinitialize");
    f.initialized = true;
    check(f.controller.connect(&f.recorder) == sdk::kResultOk, "controller reconnect");
    const auto fresh = f.owner();
    check(fresh != a && fresh != b, "reinitialize reused stale token");
    f.down(fresh, 41);
    f.close(a);
    check(f.controller.heldAuditionKey(fresh) == 41 && f.recorder.count == 4,
          "pre-termination owner affected a new view");
    f.close(fresh);
    f.recorder.note(4, 41, false, 0.0);
}
} // namespace

int main() {
    try {
        fresh_down_and_idle_close();
        transitions_are_ordered();
        shared_and_distinct_keys();
        stale_tokens_and_invalid_requests();
        zero_velocity_releases_only_its_owner();
        positive_subnormal_stays_a_down();
        bounded_registration_and_wrap();
        failed_transport_cancels_all_owners();
        pending_emergency_retries_without_fresh_down();
        failed_final_close_retains_emergency();
        termination_releases_owned_keys();
        std::cout << "{\"suite\":\"vst3_audition_ownership\",\"checks\":" << checks
                  << ",\"result\":\"pass\"}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "vst3 audition ownership failed: " << error.what() << '\n';
        return 1;
    }
}
