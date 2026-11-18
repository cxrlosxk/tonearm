// SPDX-License-Identifier: GPL-2.0-or-later
#include "provider.hpp"

namespace tonearm {

#ifndef _WIN32
namespace {
// SMTC is Windows-only. Other platforms report the provider as unavailable so the
// plugin shows its idle/disconnected state instead of failing.
class UnavailableProvider final : public MediaProvider {
public:
	void start() override
	{
		MediaSnapshot snap;
		snap.state = ProviderState::Unavailable;
		publish(std::move(snap));
	}
	void stop() override {}
};
} // namespace

std::unique_ptr<MediaProvider> createSystemProvider()
{
	return std::make_unique<UnavailableProvider>();
}
#endif

} // namespace tonearm
