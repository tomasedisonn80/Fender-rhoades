#pragma once

#include <cstdint>

namespace rhodes::vst3 {

// GUI-thread-only supplement for hosts whose X11 FocusOut is not forwarded by
// the pinned VSTGUI backend. Each editor owns an independent event connection;
// this never consumes or changes VSTGUI's connection or event registrations.
// Other platforms use the existing native focus hooks and this is a no-op.
class PhotoNativeFocus final {
public:
    PhotoNativeFocus() noexcept = default;
    ~PhotoNativeFocus();
    PhotoNativeFocus(const PhotoNativeFocus&) = delete;
    PhotoNativeFocus& operator=(const PhotoNativeFocus&) = delete;

    // Pass the public IPlatformFrame representation only for an X11 frame, or
    // zero for any other platform. A failed connection is not retried each idle.
    void attach(std::uintptr_t x11_window) noexcept;
    void reset() noexcept;

    // At most 64 queued events are consumed. The optional UI-thread round trip
    // orders already-generated focus events before a pointer event is handled.
    // A real out/back transition still reports loss even when focus is restored.
    [[nodiscard]] bool poll(bool synchronize = false) noexcept;

private:
    std::uintptr_t window_ = 0;
    void* connection_ = nullptr;
};

} // namespace rhodes::vst3
