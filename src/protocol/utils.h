#include <sstream>
#include <iomanip>
#include <vector>
#include <openssl/evp.h>
#include <openssl/rand.h>

static std::string toHex(const unsigned char *data, size_t len)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i)
        oss << std::setw(2) << (int)data[i];
    return oss.str();
}
static std::vector<unsigned char> fromHex(const std::string &hex)
{
    std::vector<unsigned char> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
    {
        unsigned int byte;
        std::istringstream(hex.substr(i, 2)) >> std::hex >> byte;
        out.push_back(static_cast<unsigned char>(byte));
    }
    return out;
}
