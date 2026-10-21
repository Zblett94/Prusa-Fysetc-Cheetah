/// @file
#include "mmu2_crc.h"

#if defined(__AVR__) || defined(PRUSA_MK3_FW)
    #include <util/crc16.h>
#endif

namespace modules {
// clang-format off
namespace crc {
#if defined(__AVR__) || defined(PRUSA_MK3_FW)
uint8_t CRC8::CCITT_update(uint8_t crc, uint8_t b) {
    return _crc8_ccitt_update(crc, b);
}
#else
uint8_t CRC8::CCITT_update(uint8_t crc, uint8_t b) {
    return CCITT_updateCX(crc, b);
}
#endif
} // namespace crc
// clang-format on
} // namespace modules
