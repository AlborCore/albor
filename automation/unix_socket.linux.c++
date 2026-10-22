#include <string>

#include "automation/automation_platform.h"
#include "automation/automation_transport.h"

namespace albor::automation::platform
{
    namespace
    {
        // the local transport, which is a unix domain socket and never a network one (§10.3, AD-10)
        class UnixSocketTransport final : public AutomationTransport
        {
        public:
            [[nodiscard]] bool listen(const AutomationEndpoint& endpoint) override {
                // TODO: implement Unix domain socket bind/listen on Linux
                (void)endpoint;
                return false;
            }

            [[nodiscard]] const std::string& session_token() const override {
                static std::string empty;
                return empty;
            }

            void set_connection_factory(ConnectionFactory factory) override { (void)factory; }

            [[nodiscard]] bool serve_one() override {
                // TODO: accept one connection, process commands
                return false;
            }

            void stop() override {
                // TODO: cleanup resources
            }

            [[nodiscard]] const std::string& last_error() const override {
                static std::string empty;
                return empty;
            }

        private:
        };
    } // namespace

    bool available() {
        // a unix domain socket exists on every linux build this project supports (§10.3, AD-10)
        return true;
    }

    std::unique_ptr<AutomationTransport> build(AutomationRegistry&) {
        // the transport is handed back by value, which the host owns (§10.3)
        return std::make_unique<UnixSocketTransport>();
    }
} // namespace albor::automation::platform
