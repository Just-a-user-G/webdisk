#include <iostream>
#include "util/crypto.h"

int main() {
    // 1. 验证 SHA256
    // "abc" 的标准 SHA256 是：
    // ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    std::string h = crypto::sha256_hex("abc");
    std::cout << "sha256(abc) = " << h << "\n";
    std::cout << "expect      = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n";
    std::cout << "match: " << (h == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" ? "YES" : "NO") << "\n\n";

    // 2. 验证随机数：连续两次应该不同，长度是 bytes*2
    std::string r1 = crypto::random_hex(16);
    std::string r2 = crypto::random_hex(16);
    std::cout << "random_hex(16) #1 = " << r1 << " (len=" << r1.size() << ")\n";
    std::cout << "random_hex(16) #2 = " << r2 << " (len=" << r2.size() << ")\n";
    std::cout << "different: " << (r1 != r2 ? "YES" : "NO") << "\n";
    std::cout << "len is 32: " << (r1.size() == 32 ? "YES" : "NO") << "\n";

    return 0;
}
