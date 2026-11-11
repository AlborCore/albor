#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "albor/compiler/naming.h"

using namespace albor::compiler;

TEST(Naming, EveryWordBoundaryRendersAsSnakeCase) {
    // arrange the names a component or record may declare, because the compiler and the glue share this spelling (§11.3)
    const std::vector<std::pair<std::string_view, std::string_view>> cases{
        {"Task", "task"}, {"WaveformView", "waveform_view"}, {"XMLView", "xml_view"}, {"URLParser", "url_parser"}, {"URL", "url"}, {"Panel2View", "panel2_view"}, {"AB1cDef", "ab1c_def"}, {"ABCdE", "ab_cd_e"}, {"A1BCd", "a1_b_cd"}, {"HTTP2Server", "http2_server"}, {"myWidget", "my_widget"}, {"XMLHTTPResponse", "xmlhttp_response"}, {"A", "a"}, {"", ""},
    };
    // act and assert each name renders as the file base the generator writes (§11.3, §14.1)
    for (const std::pair<std::string_view, std::string_view>& entry : cases) {
        // the written name is traced, so a failing row names the case that broke (§11.3)
        SCOPED_TRACE(entry.first);
        EXPECT_EQ(snake_case(entry.first), entry.second);
    }
}
