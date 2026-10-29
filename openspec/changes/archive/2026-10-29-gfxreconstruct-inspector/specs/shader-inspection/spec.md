## ADDED Requirements

### Requirement: Native text input and IME
The custom editor SHALL request modern ImGui/SDL text input and anchor IME composition to the caret. IME composition keys SHALL not mutate the document; committed UTF-8 text SHALL enter through the SDL backend. Native text-entry tests SHALL complement direct Test Engine character injection so missing OS text-input activation cannot pass unnoticed.

#### Scenario: Enter committed native text
- **WHEN** the focused editable shader receives a committed SDL text packet, including Chinese text
- **THEN** the characters enter the document while Backspace/Enter used during composition do not delete or add document lines

### Requirement: Stable captured shader identity
The application SHALL extract or recover captured SPIR-V and link it to creation identity, lifetime, binary hash, stage, entry point, specialization constants, and referencing pipelines. Different module identities SHALL remain distinct even when their binary content is identical.

#### Scenario: Select a pipeline stage
- **WHEN** the selected event references a supported stage with captured SPIR-V
- **THEN** the shader view shows its captured identity, entry point, specialization values, and binary-backed representations

#### Scenario: Identical binary is used by two modules
- **WHEN** the capture creates two modules from identical SPIR-V bytes
- **THEN** the application can share derived text caches while preserving separate captured module and replay replacement identities

### Requirement: Readable shader representations
The application SHALL provide SPIRV-Tools assembly and SPIRV-Cross-generated Vulkan GLSL with line numbers, syntax coloring, search, and copy/export. It SHALL show available reflected descriptor bindings, push constants, stage interfaces, and execution metadata.

#### Scenario: View a supported Vulkan shader
- **WHEN** extraction, validation, and cross-compilation succeed
- **THEN** the user can switch between assembly and generated GLSL and inspect reflected shader interfaces

#### Scenario: Cross-compilation fails
- **WHEN** SPIRV-Cross cannot represent a captured extension or instruction
- **THEN** the application reports the cause and keeps available binary, assembly, and metadata accessible

### Requirement: Source provenance
The application SHALL distinguish reconstructed GLSL from source text/debug metadata embedded in the capture and SHALL identify absent original source, stripped names, or missing mappings. Generated source SHALL NOT be labeled as recovered original source.

#### Scenario: Original source was stripped
- **WHEN** a module contains no original source/debug mapping
- **THEN** its GLSL view is labeled reconstructed and original source/line mapping is shown as unavailable

#### Scenario: Source metadata is available
- **WHEN** embedded source or debug metadata can be decoded
- **THEN** it is exposed separately with its origin and any mapping limits

### Requirement: Fast custom GLSL and SPIR-V highlighting
Shader source views SHALL use pinned BalazsJako/ImGuiColorTextEdit. GLSL and SPIR-V SHALL use custom tokenizer callbacks with empty regex token lists and linear scanning in the tokenizer hot path. GLSL highlighting SHALL distinguish keywords/types, built-ins, identifiers, literals, operators, preprocessor directives, and line/block comments. SPIR-V highlighting SHALL distinguish opcodes, result/reference IDs, enum operands, literals, punctuation, and semicolon comments.

#### Scenario: Highlight GLSL with preprocessing and multiline comments
- **WHEN** GLSL containing layout qualifiers, built-ins, numeric literals, preprocessor directives, and multiline comments is viewed or edited
- **THEN** the custom tokenizer and comment scanner assign the expected categories and refresh changed text without invoking regex rules

#### Scenario: Highlight SPIR-V assembly
- **WHEN** assembly contains an opcode, `%` result/reference IDs, enum operands, quoted strings, numeric operands, and semicolon comments
- **THEN** those elements receive their intended categories without treating semicolon comments as GLSL comments

#### Scenario: Inspect a large shader
- **WHEN** a shader with at least 10,000 lines is opened, scrolled, or edited
- **THEN** incremental highlighting preserves responsive GUI input and the measured tokenization/edit latency is recorded alongside the machine and document size
