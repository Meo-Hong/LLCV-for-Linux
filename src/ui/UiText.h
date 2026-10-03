#pragma once

namespace llcv::ui_text {

// Returns the original pointer for Korean/unknown text, or a process-lifetime
// translation. No language/settings globals are read by this module.
const wchar_t* Translate(const wchar_t* korean, bool useEnglish);

// Instructions only: does not query the GPU or claim VSR is active.
const wchar_t* VsrSetupGuide(bool useEnglish);

} // namespace llcv::ui_text
