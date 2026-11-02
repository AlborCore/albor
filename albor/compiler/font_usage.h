#pragma once

#include "albor/compiler/diagnostic.h"
#include "albor/compiler/font_catalog.h"
#include "albor/compiler/type_checker.h"

namespace albor::compiler
{
    // check every declarative font selection against the faces the target carries (§11.1, AD-34)
    void check_font_usage(const CheckedFile& file, const FontCatalog& catalog, DiagnosticList& diagnostics);
} // namespace albor::compiler
