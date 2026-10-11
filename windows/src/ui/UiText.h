#pragma once

namespace llcv::ui_text {

// Returns the original pointer for Korean/unknown text, or a process-lifetime
// translation. After dictionary initialization, lookups allocate no temporary
// strings and retain no caller storage. No language/settings globals are read.
const wchar_t* Translate(const wchar_t* korean, bool useEnglish);

// Instructions only: does not query the GPU or claim VSR is active.
const wchar_t* VsrSetupGuide(bool useEnglish);

} // namespace llcv::ui_text
