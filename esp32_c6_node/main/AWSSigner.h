#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <ctime>
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"
#include "esp_log.h"

class AWSSigner
{
public:
    struct Config
    {
        std::string accessKey;
        std::string secretKey;
        std::string region;
        std::string service; // e.g., "execute-api" or "iotdata"
        std::string host;    // e.g., "api-id.execute-api.us-east-1.amazonaws.com"
    };

    AWSSigner(Config config) : cfg(config) {}

    // Returns the headers needed for the request (including Authorization and x-amz-date)
    std::vector<std::pair<std::string, std::string>> sign(
        const std::string &method,
        const std::string &uri,
        const std::string &payload,
        const std::string &queryParams = "")
    {
        // 1. Get current time
        time_t now;
        time(&now);
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);

        char dateStr[9];  // YYYYMMDD
        char timeStr[17]; // YYYYMMDDTHHMMSSZ
        strftime(dateStr, sizeof(dateStr), "%Y%m%d", &timeinfo);
        strftime(timeStr, sizeof(timeStr), "%Y%m%dT%H%M%SZ", &timeinfo);

        std::string amzDate = timeStr;
        std::string dateStamp = dateStr;

        // 2. Hash Payload
        std::string payloadHash = sha256_hex(payload);

        // 3. Create Canonical Request
        // CanonicalHeaders must be sorted by name
        std::string canonicalHeaders =
            "host:" + cfg.host + "\n" +
            "x-amz-date:" + amzDate + "\n";

        std::string signedHeaders = "host;x-amz-date";

        std::string canonicalRequest =
            method + "\n" +
            uri + "\n" +
            queryParams + "\n" + // Query string
            canonicalHeaders + "\n" +
            signedHeaders + "\n" +
            payloadHash;

        // 4. String to Sign
        std::string algorithm = "AWS4-HMAC-SHA256";
        std::string credentialScope = dateStamp + "/" + cfg.region + "/" + cfg.service + "/aws4_request";

        std::string stringToSign =
            algorithm + "\n" +
            amzDate + "\n" +
            credentialScope + "\n" +
            sha256_hex(canonicalRequest);

        // 5. Calculate Signature Key (Recursive HMAC)
        std::vector<unsigned char> kDate = hmac("AWS4" + cfg.secretKey, dateStamp);
        std::vector<unsigned char> kRegion = hmac(kDate, cfg.region);
        std::vector<unsigned char> kService = hmac(kRegion, cfg.service);
        std::vector<unsigned char> kSigning = hmac(kService, "aws4_request");
        std::vector<unsigned char> signature = hmac(kSigning, stringToSign);

        std::string signatureHex = to_hex(signature);

        // 6. Construct Authorization Header
        std::string authHeader =
            algorithm + " Credential=" + cfg.accessKey + "/" + credentialScope +
            ", SignedHeaders=" + signedHeaders +
            ", Signature=" + signatureHex;

        return {
            {"Authorization", authHeader},
            {"x-amz-date", amzDate},
            {"x-amz-content-sha256", payloadHash} // Often required by AWS
        };
    }

private:
    Config cfg;

    std::string sha256_hex(const std::string &data)
    {
        unsigned char hash[32];
        mbedtls_sha256((const unsigned char *)data.c_str(), data.length(), hash, 0);
        return to_hex(std::vector<unsigned char>(hash, hash + 32));
    }

    std::vector<unsigned char> hmac(const std::string &key, const std::string &msg)
    {
        return hmac(std::vector<unsigned char>(key.begin(), key.end()), msg);
    }

    std::vector<unsigned char> hmac(const std::vector<unsigned char> &key, const std::string &msg)
    {
        unsigned char output[32];
        mbedtls_md_context_t ctx;
        mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

        mbedtls_md_init(&ctx);
        mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
        mbedtls_md_hmac_starts(&ctx, key.data(), key.size());
        mbedtls_md_hmac_update(&ctx, (const unsigned char *)msg.c_str(), msg.length());
        mbedtls_md_hmac_finish(&ctx, output);
        mbedtls_md_free(&ctx);

        return std::vector<unsigned char>(output, output + 32);
    }

    std::string to_hex(const std::vector<unsigned char> &data)
    {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (unsigned char c : data)
            ss << std::setw(2) << (int)c;
        return ss.str();
    }
};