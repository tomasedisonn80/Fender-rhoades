#include "plugins/vst3/controller.hpp"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

// Exercise the real controller through its public owner API and SDK IMessage
// connection. Literal V1 IDs, attributes and wire order are the oracle. The
// receiver applies accepted messages to an independent 73-key held bitmap; it
// does not call processor code or reproduce the controller's ownership logic.
// This fixture has no view, window, audio callback or private controller access.
namespace {
namespace sdk = Steinberg;
namespace vst = Steinberg::Vst;
using Controller = rhodes::vst3::RhodesController;
using Owner = Controller::AuditionOwner;

std::size_t checks = 0;
std::size_t scenarios = 0;
std::string current_case;

void check(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

void scenario(std::string name) {
    current_case = std::move(name);
    ++scenarios;
}

enum class Site : std::size_t {
    allocate,
    message_id,
    attributes,
    key,
    down,
    velocity,
    note_before_accept,
    note_after_accept,
    release_before_accept,
    release_after_accept,
    count
};

constexpr std::array construction_sites{
    Site::allocate, Site::message_id, Site::attributes,
    Site::key, Site::down, Site::velocity};
constexpr std::array note_sites{
    Site::allocate, Site::message_id, Site::attributes,
    Site::key, Site::down, Site::velocity,
    Site::note_before_accept, Site::note_after_accept};
constexpr std::array release_sites{
    Site::allocate, Site::release_before_accept, Site::release_after_accept};

std::string_view name(Site site) {
    constexpr std::array<std::string_view, static_cast<std::size_t>(Site::count)> names{
        "allocate", "message_id", "attributes", "key", "down", "velocity",
        "note_before_accept", "note_after_accept",
        "release_before_accept", "release_after_accept"};
    return names[static_cast<std::size_t>(site)];
}

bool in_notify(Site site) {
    return site == Site::note_before_accept || site == Site::note_after_accept;
}

struct Hooks {
    struct Once {
        std::function<void()> callback;
        std::size_t calls = 0;
    };
    std::array<Once, static_cast<std::size_t>(Site::count)> slots{};

    void arm(Site site, std::function<void()> callback) {
        auto& slot = slots[static_cast<std::size_t>(site)];
        check(!slot.callback && slot.calls == 0, "hook unexpectedly reused");
        slot.callback = std::move(callback);
    }
    void invoke(Site site) {
        auto& slot = slots[static_cast<std::size_t>(site)];
        if (!slot.callback) { return; }
        // Disarm before invoking user code. Nested allocation/notify calls do
        // not recursively invoke the same hook or manufacture a test storm.
        auto callback = std::exchange(slot.callback, {});
        ++slot.calls;
        callback();
    }
    void expect_once(Site site) const {
        const auto& slot = slots[static_cast<std::size_t>(site)];
        check(slot.calls == 1 && !slot.callback, "one-shot hook did not run exactly once");
    }
    void clear() {
        for (auto& slot : slots) { slot.callback = {}; }
    }
};

struct Depth {
    std::size_t current = 0;
    std::size_t maximum = 0;
    struct Scope {
        Depth& depth;
        explicit Scope(Depth& value) : depth(value) {
            ++depth.current;
            depth.maximum = std::max(depth.maximum, depth.current);
        }
        ~Scope() { --depth.current; }
    };
};

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
    Hooks* hooks = nullptr;
    sdk::int64 key = -1;
    sdk::int64 down = -1;
    double velocity = -1.0;

    sdk::tresult PLUGIN_API setInt(AttrID id, sdk::int64 value) override {
        if (std::strcmp(id, "key") == 0) {
            key = value;
            hooks->invoke(Site::key);
            return sdk::kResultOk;
        }
        if (std::strcmp(id, "down") == 0) {
            down = value;
            hooks->invoke(Site::down);
            return sdk::kResultOk;
        }
        return sdk::kResultFalse;
    }
    sdk::tresult PLUGIN_API getInt(AttrID id, sdk::int64& value) override {
        if (std::strcmp(id, "key") == 0) { value = key; return sdk::kResultOk; }
        if (std::strcmp(id, "down") == 0) { value = down; return sdk::kResultOk; }
        return sdk::kResultFalse;
    }
    sdk::tresult PLUGIN_API setFloat(AttrID id, double value) override {
        if (std::strcmp(id, "velocity") != 0) { return sdk::kResultFalse; }
        velocity = value;
        hooks->invoke(Site::velocity);
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
    Hooks* hooks = nullptr;
    const char* id = nullptr;
    Attributes attributes{};

    sdk::FIDString PLUGIN_API getMessageID() override { return id; }
    void PLUGIN_API setMessageID(sdk::FIDString value) override {
        id = value;
        if (id != nullptr && std::strcmp(id, "RhodesPhotoNoteV1") == 0) {
            hooks->invoke(Site::message_id);
        }
    }
    vst::IAttributeList* PLUGIN_API getAttributes() override {
        hooks->invoke(Site::attributes);
        return &attributes;
    }
};

struct Application final : FixedObject<vst::IHostApplication> {
    Hooks& hooks;
    std::array<Message, 64> messages{};
    std::size_t allocated = 0;
    std::size_t attempts = 0;
    std::size_t fail_allocations = 0;
    Depth depth;
    bool exceeded = false;

    explicit Application(Hooks& value) : hooks(value) {}
    sdk::tresult PLUGIN_API getName(vst::String128 value) override {
        value[0] = 'T';
        value[1] = 0;
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
        Depth::Scope scope{depth};
        ++attempts;
        if (depth.current > 8 || attempts > 128 || allocated == messages.size()) {
            exceeded = true;
            return sdk::kResultFalse;
        }
        if (fail_allocations != 0) { --fail_allocations; return sdk::kResultFalse; }
        // Reserve first. Each nested send receives different storage, so an
        // emergency cannot overwrite the original down's ID or attributes.
        auto& message = messages[allocated++];
        message.hooks = &hooks;
        message.attributes.hooks = &hooks;
        hooks.invoke(Site::allocate);
        *object = static_cast<vst::IMessage*>(&message);
        return sdk::kResultOk;
    }
};

struct Receiver final : FixedObject<vst::IConnectionPoint> {
    enum class Kind { note, release };
    struct Record {
        Kind kind = Kind::note;
        sdk::int64 key = -1;
        sdk::int64 down = -1;
        double velocity = -1.0;
        bool accepted = false;
    };
    Hooks& hooks;
    std::array<Record, 64> records{};
    std::array<bool, 73> held{};
    std::size_t count = 0;
    std::size_t fail_releases = 0;
    Depth depth;
    bool malformed = false;
    bool exceeded = false;

    explicit Receiver(Hooks& value) : hooks(value) {}
    sdk::tresult PLUGIN_API connect(vst::IConnectionPoint*) override { return sdk::kResultOk; }
    sdk::tresult PLUGIN_API disconnect(vst::IConnectionPoint*) override { return sdk::kResultOk; }
    sdk::tresult PLUGIN_API notify(vst::IMessage* message) override {
        Depth::Scope scope{depth};
        if (depth.current > 8 || count == records.size()) {
            exceeded = true;
            return sdk::kResultFalse;
        }
        if (message == nullptr || message->getMessageID() == nullptr) {
            malformed = true;
            return sdk::kResultFalse;
        }
        Record record;
        if (std::strcmp(message->getMessageID(), "RhodesPhotoReleaseV1") == 0) {
            record.kind = Kind::release;
            record.accepted = fail_releases == 0;
            if (fail_releases != 0) { --fail_releases; }
            hooks.invoke(Site::release_before_accept);
        } else if (std::strcmp(message->getMessageID(), "RhodesPhotoNoteV1") == 0) {
            auto* attributes = message->getAttributes();
            if (attributes == nullptr ||
                attributes->getInt("key", record.key) != sdk::kResultOk ||
                attributes->getInt("down", record.down) != sdk::kResultOk ||
                attributes->getFloat("velocity", record.velocity) != sdk::kResultOk ||
                record.key < 0 || record.key >= 73 ||
                (record.down != 0 && record.down != 1) ||
                !std::isfinite(record.velocity) || record.velocity < 0.0 || record.velocity > 1.0) {
                malformed = true;
                return sdk::kResultFalse;
            }
            record.accepted = true;
            if (record.down == 1) { hooks.invoke(Site::note_before_accept); }
        } else {
            malformed = true;
            return sdk::kResultFalse;
        }
        if (count == records.size()) { exceeded = true; return sdk::kResultFalse; }
        // Commit the parsed, independent record exactly where the receiver
        // accepts it. In particular, a before hook's nested release precedes
        // this down on the wire; an after hook's release follows it.
        records[count++] = record;
        if (record.accepted) {
            if (record.kind == Kind::release) { held.fill(false); }
            else { held[static_cast<std::size_t>(record.key)] = record.down == 1; }
        }
        if (record.kind == Kind::release) { hooks.invoke(Site::release_after_accept); }
        else if (record.down == 1) { hooks.invoke(Site::note_after_accept); }
        return record.accepted ? sdk::kResultOk : sdk::kResultFalse;
    }

    std::string trace() const {
        std::string result;
        for (std::size_t index = 0; index < count; ++index) {
            if (!result.empty()) { result += ", "; }
            const auto& record = records[index];
            if (record.kind == Kind::release) { result += "ReleaseAll"; }
            else {
                result += record.down == 1 ? "Down(" : "Up(";
                result += std::to_string(record.key) + ')';
            }
            result += record.accepted ? "+" : "-";
        }
        return '[' + result + ']';
    }
    void expect(bool condition, std::string_view message) const {
        check(condition, std::string{message} + "; wire=" + trace());
    }
    void note(std::size_t index, int key, bool down, double velocity = 0.75) const {
        expect(!malformed && index < count, "missing or malformed note message");
        const auto& record = records[index];
        expect(record.kind == Kind::note && record.key == key &&
               record.down == (down ? 1 : 0) && record.velocity == velocity && record.accepted,
               "note differs from literal wire sequence");
    }
    void release_at(std::size_t index, bool accepted = true) const {
        expect(!malformed && index < count, "missing or malformed emergency message");
        expect(records[index].kind == Kind::release && records[index].accepted == accepted,
               "emergency differs from literal wire sequence");
    }
    void expect_held(std::initializer_list<int> keys) const {
        std::array<bool, 73> expected{};
        for (const int key : keys) { expected[static_cast<std::size_t>(key)] = true; }
        expect(!malformed && held == expected, "receiver-held keys differ from literal expectation");
    }
};

struct Fixture {
    Hooks hooks;
    Application application{hooks};
    Receiver receiver{hooks};
    Controller controller;
    bool initialized = false;

    Fixture() {
        check(controller.initialize(&application) == sdk::kResultOk, "controller initialization");
        initialized = true;
        check(controller.connect(&receiver) == sdk::kResultOk, "SDK connection");
    }
    ~Fixture() {
        hooks.clear();
        application.fail_allocations = 0;
        receiver.fail_releases = 0;
        if (initialized) { (void)controller.terminate(); }
    }
    Owner owner() {
        const auto result = controller.registerAuditionOwner();
        check(result != 0, "owner registration");
        return result;
    }
    void down(Owner owner, int key, float velocity = 0.75F) {
        receiver.expect(controller.auditionNote(owner, key, true, velocity), "owned down rejected");
    }
    void up(Owner owner, int key) {
        receiver.expect(controller.auditionNote(owner, key, false, 0.0F), "owned up rejected");
    }
    void unowned(Owner a, Owner b) const {
        receiver.expect(controller.heldAuditionKey(a) == -1 &&
                        controller.heldAuditionKey(b) == -1,
                        "emergency retained or resurrected controller ownership");
    }
    void bounded() const {
        check(!application.exceeded && !receiver.exceeded && !receiver.malformed,
              "message budget, reentrancy bound or wire schema violated");
        check(application.depth.current == 0 && receiver.depth.current == 0,
              "SDK callback did not unwind");
    }
    void idle_retry_sends_nothing() {
        const auto count = receiver.count;
        const auto attempts = application.attempts;
        receiver.expect(controller.retryAuditionRelease(), "completed emergency remained pending");
        receiver.expect(receiver.count == count && application.attempts == attempts,
                        "idle retry emitted an unnecessary emergency");
    }
};

void construction_emergency() {
    for (const auto site : construction_sites) {
        scenario("construction/" + std::string{name(site)});
        Fixture f;
        const auto sibling = f.owner();
        const auto active = f.owner();
        f.down(sibling, 12);
        f.hooks.arm(site, [&] {
            f.receiver.expect(f.controller.releaseAuditionNotes(), "nested emergency rejected");
            f.unowned(sibling, active);
            f.receiver.expect_held({});
        });
        const bool accepted = f.controller.auditionNote(active, 40, true, 0.75F);
        f.hooks.expect_once(site);
        f.unowned(sibling, active);
        f.receiver.expect_held({});
        f.receiver.expect(!accepted, "cancelled construction reported an accepted down");
        f.receiver.note(0, 12, true);
        f.receiver.release_at(1);
        f.receiver.expect(f.receiver.count == 2,
                          "stale down or redundant emergency followed construction-time cancellation");
        f.up(active, 40);
        f.idle_retry_sends_nothing();
        f.receiver.expect(f.receiver.count == 2, "stale owner cleanup appended traffic");
        f.down(active, 42, 0.5F);
        f.receiver.note(2, 42, true, 0.5);
        f.receiver.expect_held({42});
        f.up(active, 42);
        f.receiver.note(3, 42, false, 0.0);
        f.receiver.expect_held({});
        f.bounded();
    }
}

void notify_emergency() {
    for (const auto site : {Site::note_before_accept, Site::note_after_accept}) {
        scenario("notify/" + std::string{name(site)});
        Fixture f;
        const auto sibling = f.owner();
        const auto active = f.owner();
        f.down(sibling, 12);
        f.hooks.arm(site, [&] {
            f.receiver.expect(f.controller.releaseAuditionNotes(), "notify emergency rejected");
            f.unowned(sibling, active);
            f.receiver.expect_held({});
        });
        const bool accepted = f.controller.auditionNote(active, 40, true, 0.75F);
        f.hooks.expect_once(site);
        f.unowned(sibling, active);
        f.receiver.expect_held({});
        f.receiver.expect(!accepted, "cancelled notify reported an accepted owned down");
        f.receiver.note(0, 12, true);
        if (site == Site::note_before_accept) {
            f.receiver.release_at(1);
            f.receiver.note(2, 40, true);
            f.receiver.release_at(3);
            f.receiver.expect(f.receiver.count == 4,
                              "notify-before-accept lacked exactly one final compensation");
        } else {
            f.receiver.note(1, 40, true);
            f.receiver.release_at(2);
            // The sender cannot know where the receiver accepted its down.
            // Either the nested release suffices or one explicit compensation
            // follows; both have the same observable safe final state.
            f.receiver.expect(f.receiver.count == 3 || f.receiver.count == 4,
                              "notify-after-accept caused a recursive emergency storm");
            if (f.receiver.count == 4) { f.receiver.release_at(3); }
        }
        f.up(active, 40);
        f.idle_retry_sends_nothing();
        const auto next = f.receiver.count;
        f.down(active, 42);
        f.receiver.note(next, 42, true);
        f.receiver.expect_held({42});
        f.up(active, 42);
        f.receiver.expect_held({});
        f.bounded();
    }
    for (const bool allocation_failure : {false, true}) {
        scenario(std::string{"notify/failed_compensation/"} +
                 (allocation_failure ? "allocation_failure" : "notify_failure"));
        Fixture f;
        const auto active = f.owner();
        f.hooks.arm(Site::note_before_accept, [&] {
            f.receiver.expect(f.controller.releaseAuditionNotes(),
                              "initial notify emergency rejected");
            f.receiver.expect_held({});
            // The first emergency is accepted before the original down. Only
            // the later, necessary compensation is made to fail.
            if (allocation_failure) { f.application.fail_allocations = 1; }
            else { f.receiver.fail_releases = 1; }
        });
        const bool accepted = f.controller.auditionNote(active, 40, true, 0.75F);
        f.hooks.expect_once(Site::note_before_accept);
        f.receiver.expect(!accepted, "failed compensation reported an accepted owned down");
        f.receiver.release_at(0);
        f.receiver.note(1, 40, true);
        f.receiver.expect(f.application.attempts == 3 &&
                          f.receiver.count == (allocation_failure ? 2U : 3U),
                          "failed compensation was missing or retried recursively");
        if (!allocation_failure) { f.receiver.release_at(2, false); }
        f.receiver.expect_held({40});
        f.receiver.expect(f.controller.heldAuditionKey(active) == -1,
                          "failed compensation restored cancelled ownership");
        const auto count = f.receiver.count;
        f.receiver.expect(!f.controller.auditionNote(active, 42, true, 0.5F),
                          "compensation recovery accepted a fresh down in the same call");
        f.receiver.release_at(count);
        f.receiver.expect(f.receiver.count == count + 1 && f.application.attempts == 4,
                          "compensation recovery emitted extra traffic");
        f.receiver.expect_held({});
        f.receiver.expect(f.controller.heldAuditionKey(active) == -1,
                          "recovery acquired fresh ownership");
        f.idle_retry_sends_nothing();
        f.down(active, 42, 0.5F);
        f.receiver.note(count + 1, 42, true, 0.5);
        f.up(active, 42);
        f.receiver.expect_held({});
        f.bounded();
    }
}

void pending_emergency() {
    for (const auto site : note_sites) {
        for (const bool allocation_failure : {false, true}) {
            scenario("pending/" + std::string{name(site)} +
                     (allocation_failure ? "/allocation_failure" : "/notify_failure"));
            Fixture f;
            const auto sibling = f.owner();
            const auto active = f.owner();
            f.down(sibling, 12);
            const auto fail = [&](std::size_t count) {
                if (allocation_failure) { f.application.fail_allocations = count; }
                else { f.receiver.fail_releases = count; }
            };
            f.hooks.arm(site, [&] {
                // More failures than any permitted immediate compensation.
                // Later explicit resets model host transport recovery.
                fail(8);
                f.receiver.expect(!f.controller.releaseAuditionNotes(),
                                  "failed nested emergency reported success");
                f.unowned(sibling, active);
            });
            const auto allocations = f.application.attempts;
            const bool accepted = f.controller.auditionNote(active, 40, true, 0.75F);
            f.hooks.expect_once(site);
            f.unowned(sibling, active);
            f.receiver.expect(!accepted, "down succeeded across a failed nested emergency");
            const auto immediate_attempts = f.application.attempts - allocations;
            f.receiver.expect(immediate_attempts >= 2 &&
                              immediate_attempts <= (in_notify(site) ? 3U : 2U),
                              "failed emergency recursively retried during the original down");
            std::size_t down_count = 0;
            for (std::size_t index = 1; index < f.receiver.count; ++index) {
                const auto& record = f.receiver.records[index];
                if (record.kind == Receiver::Kind::note) {
                    ++down_count;
                    f.receiver.note(index, 40, true);
                } else { f.receiver.release_at(index, false); }
            }
            f.receiver.expect(down_count == (in_notify(site) ? 1U : 0U),
                              "failed construction emergency admitted a stale down");
            if (in_notify(site)) { f.receiver.expect_held({12, 40}); }
            else { f.receiver.expect_held({12}); }

            fail(1);
            auto count = f.receiver.count;
            auto attempts = f.application.attempts;
            f.receiver.expect(!f.controller.auditionNote(active, 42, true, 0.5F),
                              "fresh down bypassed a failed pending emergency");
            f.receiver.expect(f.application.attempts == attempts + 1,
                              "one recovery call performed recursive retries");
            f.receiver.expect(f.receiver.count == count + (allocation_failure ? 0U : 1U),
                              "failed recovery appended a fresh down");
            if (!allocation_failure) { f.receiver.release_at(count, false); }
            f.unowned(sibling, active);

            fail(0);
            count = f.receiver.count;
            attempts = f.application.attempts;
            f.receiver.expect(!f.controller.auditionNote(active, 42, true, 0.5F),
                              "successful recovery also accepted a fresh down in the same call");
            f.receiver.expect(f.application.attempts == attempts + 1 &&
                              f.receiver.count == count + 1,
                              "successful recovery emitted more than its emergency");
            f.receiver.release_at(count);
            f.receiver.expect_held({});
            f.unowned(sibling, active);
            f.idle_retry_sends_nothing();
            f.down(active, 42, 0.5F);
            f.receiver.note(count + 1, 42, true, 0.5);
            f.receiver.expect_held({42});
            f.up(active, 42);
            f.receiver.expect_held({});
            f.bounded();
        }
    }
}

void sibling_cleanup() {
    for (const auto site : note_sites) {
        for (const bool same_key : {false, true}) {
            scenario("siblings/" + std::string{name(site)} +
                     (same_key ? "/same_key" : "/distinct_keys"));
            Fixture f;
            const auto sibling = f.owner();
            const auto active = f.owner();
            const int sibling_key = same_key ? 40 : 12;
            f.down(sibling, sibling_key, 0.25F);
            f.hooks.arm(site, [&] {
                f.receiver.expect(f.controller.unregisterAuditionOwner(sibling),
                                  "ordinary sibling close rejected");
            });
            f.down(active, 40);
            f.hooks.expect_once(site);
            f.receiver.expect(f.controller.heldAuditionKey(sibling) == -1 &&
                              f.controller.heldAuditionKey(active) == 40,
                              "ordinary sibling cleanup cancelled the active owner");
            f.receiver.note(0, sibling_key, true, 0.25);
            if (same_key) {
                f.receiver.note(1, 40, true);
                f.receiver.expect(f.receiver.count == 2, "shared-key cleanup sent a premature up");
            } else if (site == Site::note_after_accept) {
                f.receiver.note(1, 40, true);
                f.receiver.note(2, 12, false, 0.0);
                f.receiver.expect(f.receiver.count == 3, "distinct cleanup emitted extra traffic");
            } else {
                f.receiver.note(1, 12, false, 0.0);
                f.receiver.note(2, 40, true);
                f.receiver.expect(f.receiver.count == 3, "distinct cleanup emitted extra traffic");
            }
            f.receiver.expect_held({40});
            f.idle_retry_sends_nothing();
            const auto before = f.receiver.count;
            f.up(active, 40);
            f.receiver.note(before, 40, false, 0.0);
            f.receiver.expect(f.receiver.count == before + 1,
                              "last owner did not send exactly one up");
            f.receiver.expect_held({});
            f.receiver.expect(f.controller.unregisterAuditionOwner(sibling),
                              "idempotent sibling cleanup rejected");
            f.receiver.expect(f.receiver.count == before + 1,
                              "repeated sibling cleanup emitted traffic");
            f.bounded();
        }
    }
}

void coalesced_emergency() {
    for (const auto site : release_sites) {
        for (const bool reject_outer : {false, true}) {
            scenario("coalescing/" + std::string{name(site)} +
                     (reject_outer ? "/rejected" : "/accepted"));
            Fixture f;
            const auto active = f.owner();
            f.down(active, 40);
            f.receiver.fail_releases = reject_outer ? 1U : 0U;
            f.hooks.arm(site, [&] {
                const auto attempts = f.application.attempts;
                const auto count = f.receiver.count;
                // These public requests occur while the first release is in
                // flight. They must coalesce without recursive transport.
                (void)f.controller.releaseAuditionNotes();
                (void)f.controller.retryAuditionRelease();
                f.receiver.expect(!f.controller.auditionNote(active, 41, true, 0.75F),
                                  "down entered an in-flight emergency");
                f.receiver.expect(f.application.attempts == attempts && f.receiver.count == count,
                                  "in-flight emergency recursively allocated or sent messages");
                f.receiver.expect(f.controller.heldAuditionKey(active) == -1,
                                  "coalesced emergency resurrected ownership");
            });
            const bool accepted = f.controller.releaseAuditionNotes();
            f.hooks.expect_once(site);
            f.receiver.expect(accepted != reject_outer, "outer emergency result lost transport outcome");
            f.receiver.note(0, 40, true);
            f.receiver.release_at(1, !reject_outer);
            f.receiver.expect(f.receiver.count == 2 && f.application.attempts == 2,
                              "coalesced emergency sent more than one release");
            if (reject_outer) {
                f.receiver.expect_held({40});
                f.receiver.expect(!f.controller.auditionNote(active, 41, true, 0.75F),
                                  "coalesced emergency recovery appended a fresh down");
                f.receiver.release_at(2);
                f.receiver.expect(f.receiver.count == 3, "coalesced recovery emitted extra traffic");
            }
            f.receiver.expect_held({});
            f.receiver.expect(f.controller.heldAuditionKey(active) == -1,
                              "completed emergency retained ownership");
            f.idle_retry_sends_nothing();
            const auto count = f.receiver.count;
            f.down(active, 41);
            f.receiver.note(count, 41, true);
            f.up(active, 41);
            f.receiver.expect_held({});
            f.receiver.expect(f.application.depth.maximum == 1 && f.receiver.depth.maximum == 1,
                              "release coalescing nested an SDK transport call");
            f.bounded();
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc > 2) { throw std::runtime_error("expected at most one selector"); }
        const std::string_view selected = argc == 2 ? argv[1] : "all";
        bool matched = false;
        const auto run = [&](std::string_view selector, auto test) {
            if (selected == "all" || selected == selector) { matched = true; test(); }
        };
        run("construction", construction_emergency);
        run("notify", notify_emergency);
        run("pending", pending_emergency);
        run("siblings", sibling_cleanup);
        run("coalescing", coalesced_emergency);
        if (!matched) { throw std::runtime_error("unknown selector: " + std::string{selected}); }
        std::cout << "{\"suite\":\"vst3_audition_reentrancy\",\"selector\":\"" << selected
                  << "\",\"scenarios\":" << scenarios << ",\"checks\":" << checks
                  << ",\"result\":\"pass\"}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "vst3 audition reentrancy failed [" << current_case << "]: "
                  << error.what() << '\n';
        return 1;
    }
}
