#pragma once

namespace Steinberg { class IPlugView; }

namespace rhodes::vst3 {

class RhodesController;

// The returned view owns one VST3 reference. Its caller releases that reference.
// The photograph is loaded by the native editor, never by an audio callback.
[[nodiscard]] Steinberg::IPlugView* create_photo_editor(RhodesController* controller);

} // namespace rhodes::vst3
