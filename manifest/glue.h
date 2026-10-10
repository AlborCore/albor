#pragma once

#include <string>
#include <vector>

#include "compiler/codegen/codegen.h"
#include "compiler/diagnostic.h"
#include "manifest/manifest.h"

namespace albor::manifest
{
    using compiler::DiagnosticList;
    using compiler::GeneratedFile;

    // the generator that turns one parsed manifest into the c++ facade classes the application implements (§11.2, AD-17)
    class GlueGenerator
    {
    public:
        // build a generator that reports problems through the given list, which never throws (§11.2)
        explicit GlueGenerator(DiagnosticList& diagnostics);

        // generate one manifest file, which is empty when a diagnostic blocked the output (§11.2)
        [[nodiscard]] std::vector<GeneratedFile> generate(const ManifestFile& file);

    private:
        // the problems the generator reports, which the caller owns (§11.2)
        DiagnosticList& m_diagnostics;
    };
} // namespace albor::manifest
