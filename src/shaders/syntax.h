#pragma once

#include <TextEditor.h>

namespace gfx {

// Allocation-free callbacks; comment state across lines is maintained by TextEditor.
bool TokenizeGlsl(const char* begin, const char* end, const char*& tokenBegin,
                  const char*& tokenEnd, TextEditor::PaletteIndex& color);
bool TokenizeSpirv(const char* begin, const char* end, const char*& tokenBegin,
                   const char*& tokenEnd, TextEditor::PaletteIndex& color);
const TextEditor::LanguageDefinition& GlslLanguage();
const TextEditor::LanguageDefinition& SpirvLanguage();

} // namespace gfx
