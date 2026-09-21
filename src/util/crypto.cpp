#include "util/crypto.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace crypto {

std::string sha256_hex(const std::string& data) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, data.data(), data.size());
    EVP_DigestFinal_ex(ctx, digest, &len);
    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    for (unsigned int i = 0; i < len; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)digest[i];
    return oss.str();
}

std::string random_hex(size_t bytes) {
    std::string buf(bytes, '\0');
    if (RAND_bytes(reinterpret_cast<unsigned char*>(buf.data()), (int)bytes) != 1)
        throw std::runtime_error("RAND_bytes failed");

    std::ostringstream oss;
    for (unsigned char c : buf)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)c;
    return oss.str();
}

Sha256Hasher::Sha256Hasher() {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    ctx_ = ctx;
}

Sha256Hasher::~Sha256Hasher() {
    if (ctx_) EVP_MD_CTX_free((EVP_MD_CTX*)ctx_);
}

void Sha256Hasher::update(const void* data, size_t len) {
    EVP_DigestUpdate((EVP_MD_CTX*)ctx_, data, len);
}

std::string Sha256Hasher::final_hex() {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_DigestFinal_ex((EVP_MD_CTX*)ctx_, digest, &len);

    std::ostringstream oss;
    for (unsigned int i = 0; i < len; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)digest[i];
    return oss.str();
}
}
