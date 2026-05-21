#pragma once

#include <cstdint>

// Hash helper functions.

// OpenSSL [v4.0.0] (https://github.com/openssl/openssl)
#include <openssl/evp.h>

enum class EvpAlgo
{
    MD5,
    SHA256,
    SHA512
};

struct EvpHashCtx
{
    EVP_MD_CTX * m_Ctx { nullptr };

    explicit EvpHashCtx( EvpAlgo algo )
        : m_Ctx( EVP_MD_CTX_new() )
    {
        const EVP_MD * md = nullptr;

        switch (algo)
        {
            case EvpAlgo::MD5:
                md = EVP_md5();
                break;

            case EvpAlgo::SHA256:
                md = EVP_sha256();
                break;

            case EvpAlgo::SHA512:
                md = EVP_sha512();
                break;
        }

        EVP_DigestInit_ex( m_Ctx, md, nullptr );
    }

    ~EvpHashCtx()
    {
        if (m_Ctx)
            EVP_MD_CTX_free( m_Ctx );
    }

    EvpHashCtx( const EvpHashCtx & ) = delete;
    EvpHashCtx & operator=( const EvpHashCtx & ) = delete;

    void Update( 
        const void * ptr, 
        std::size_t n
    )
    {
        EVP_DigestUpdate( m_Ctx, ptr, n );
    }

    void Final( 
        std::uint8_t * digest
    )
    {
        std::uint32_t len = 0;
        EVP_DigestFinal_ex( m_Ctx, digest, &len );
    }
};