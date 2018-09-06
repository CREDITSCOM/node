#include <csnode/utils.h>

namespace Credits
{
    std::ostream& operator<<(std::ostream& os, Direction dir)
    {
        switch (dir)
        {
        case Direction::PrevBlock: return os << "PrevBlock";
        case Direction::NextBlock: return os << "NextBlock";
        default:
            return os << "Wrong dir=" << (int64_t)dir;
        }
    }

    std::ostream& printHex(std::ostream& os, const char* bytes, size_t num)
    {
        char hex[] = { '0', '1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' };
        for (size_t i = 0; i < num; i++)
        {
            os << hex[(bytes[i] >> 4) & 0x0F];
            os << hex[bytes[i] & 0x0F];
        }
        return os;
    }

}
