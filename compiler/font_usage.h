#pragma once

#include "compiler/diagnostic.h"
#include "compiler/font_catalog.h"
#include "compiler/type_checker.h"

namespace albor::compiler
{
    // check every declarative font selection against the faces the target carries (§11.1, AD-34)
    void check_font_usage(const CheckedFile& file, const FontCatalog& catalog, DiagnosticList& diagnostics);
} // namespace albor::compiler
