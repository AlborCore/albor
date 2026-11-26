# albor-automation

The out-of-process Python client for Albor's automation layer. It discovers a running
Albor application through `host.json`, authenticates with the per-session token, and
speaks newline-delimited MCP over a local socket or named pipe. Every call goes through
the automation layer; the harness has no second path into the application.

The package is the client only. The application it drives must be built from Dev or Test
sources with `ALBOR_AUTOMATION` enabled, because automation is excluded from Release at
compile and link time.

## Install

Every stable release attaches a wheel and a source archive:

```sh
pip install https://github.com/AlborCore/albor/releases/download/v0.0.1/albor_automation-0.0.1-py3-none-any.whl
```

Replace the version with the release you want. Python 3.10 or newer is required; the
package has no third-party dependencies.

## Use

```python
import unittest

from albor_automation import TestSession, expect, launch


class FormTests(unittest.TestCase):

    def test_form(self) -> None:
        with TestSession(launch("/path/to/my-app"), self.id()) as app:
            expect(app.get_by_id("form_name")).to_have_value("")
            app.get_by_id("form_name").type_text("Ada Lovelace")
            expect(app.get_by_id("form_name")).to_have_value("Ada Lovelace")
            app.get_by_id("form_press").click_input()
            app.screenshot("form.png")
```

`launch()` accepts an explicit executable path. Alternatively set
`ALBOR_TEST_APPLICATION` to the application path; there is no implicit default, so a
consumer cannot silently drive a framework build artifact. `ALBOR_TEST_ARTIFACTS`
selects where a failed session writes its screenshot, tree, descriptor and output
streams, and `ALBOR_AUTOMATION_DEBUG=1` logs the protocol exchange.

The full tool surface, host discovery and a complete session are documented in
[Automation and MCP](https://github.com/AlborCore/albor/blob/main/docs/automation.md).
