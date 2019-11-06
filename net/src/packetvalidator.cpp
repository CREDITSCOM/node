#include <base58.h>
#include <lib/system/logger.hpp>
#include <net/packetvalidator.hpp>
#include <net/transport.hpp>

namespace cs {
/*private*/
PacketValidator::PacketValidator() {
    starterKey_ = cs::Zero::key;
}

/*static*/
PacketValidator& PacketValidator::instance() {
    static PacketValidator inst;
    return inst;
}

bool PacketValidator::validate(const Packet& pack) {
    bool result = (pack.isHeaderValid() && (pack.getHeadersLength() < pack.size()));
    if (!result) {
        cswarning() << "Net: packet " << pack << " is not validated";
    }
    return result;
}
}  // namespace cs
