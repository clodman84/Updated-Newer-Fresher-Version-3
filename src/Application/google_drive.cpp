#include "include/google_drive.h"

#include <curl/curl.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

namespace gdrive {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers (file-local)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// libcurl write callback — appends received bytes to a std::string
size_t curl_write_callback(char *ptr, size_t size, size_t nmemb,
                           void *userdata) {
  auto *out = static_cast<std::string *>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

// Base64url encode (no padding) — required for JWT
std::string base64url_encode(const unsigned char *data, size_t len) {
  // Standard base64
  BIO *b64 = BIO_new(BIO_f_base64());
  BIO *mem = BIO_new(BIO_s_mem());
  BIO_push(b64, mem);
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  BIO_write(b64, data, static_cast<int>(len));
  BIO_flush(b64);

  BUF_MEM *buf;
  BIO_get_mem_ptr(b64, &buf);
  std::string encoded(buf->data, buf->length);
  BIO_free_all(b64);

  // Convert to base64url: replace + with -, / with _, strip =
  for (char &c : encoded) {
    if (c == '+')
      c = '-';
    else if (c == '/')
      c = '_';
  }
  while (!encoded.empty() && encoded.back() == '=')
    encoded.pop_back();

  return encoded;
}

std::string base64url_encode(const std::string &s) {
  return base64url_encode(reinterpret_cast<const unsigned char *>(s.data()),
                          s.size());
}

// Sign data with RSA-SHA256, return base64url-encoded signature
std::string rsa_sha256_sign(const std::string &data,
                            const std::string &pem_key) {
  BIO *bio = BIO_new_mem_buf(pem_key.data(), static_cast<int>(pem_key.size()));
  if (!bio)
    throw AuthError("Failed to create BIO for private key");

  EVP_PKEY *pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (!pkey)
    throw AuthError("Failed to parse private key from PEM — check key format");

  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  if (!ctx) {
    EVP_PKEY_free(pkey);
    throw AuthError("Failed to create EVP_MD_CTX");
  }

  if (EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) <= 0) {
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    throw AuthError("EVP_DigestSignInit failed");
  }
  if (EVP_DigestSignUpdate(ctx, data.data(), data.size()) <= 0) {
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    throw AuthError("EVP_DigestSignUpdate failed");
  }

  size_t sig_len = 0;
  EVP_DigestSignFinal(ctx, nullptr, &sig_len);
  std::vector<unsigned char> sig(sig_len);
  EVP_DigestSignFinal(ctx, sig.data(), &sig_len);

  EVP_MD_CTX_free(ctx);
  EVP_PKEY_free(pkey);

  return base64url_encode(sig.data(), sig_len);
}

long long now_epoch() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

FileType mime_to_type(const std::string &mime) {
  if (mime == "application/vnd.google-apps.folder")
    return FileType::Folder;
  if (mime.empty())
    return FileType::Unknown;
  return FileType::File;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// ServiceAccountCredentials
// ─────────────────────────────────────────────────────────────────────────────

ServiceAccountCredentials
ServiceAccountCredentials::from_file(const std::string &path) {
  std::ifstream f(path);
  if (!f)
    throw DriveError("Cannot open key file: " + path);

  json j;
  try {
    f >> j;
  } catch (const json::parse_error &e) {
    throw DriveError(std::string("Failed to parse key file JSON: ") + e.what());
  }

  ServiceAccountCredentials creds;
  try {
    creds.client_email = j.at("client_email").get<std::string>();
    creds.private_key = j.at("private_key").get<std::string>();
    creds.token_uri = j.at("token_uri").get<std::string>();
  } catch (const json::out_of_range &e) {
    throw DriveError(std::string("Key file is missing expected field: ") +
                     e.what() +
                     "\nMake sure this is a service account JSON key, not an "
                     "OAuth client key.");
  }

  if (j.value("type", "") != "service_account")
    throw DriveError("Key file 'type' is not 'service_account'");

  return creds;
}

// ─────────────────────────────────────────────────────────────────────────────
// DriveClient — construction / destruction
// ─────────────────────────────────────────────────────────────────────────────

DriveClient::DriveClient(ServiceAccountCredentials credentials)
    : credentials_(std::move(credentials)), token_expiry_epoch_(0),
      curl_(nullptr) {
  curl_ = curl_easy_init();
  if (!curl_)
    throw DriveError("curl_easy_init() failed");
}

DriveClient::DriveClient(DriveClient &&other) noexcept
    : credentials_(std::move(other.credentials_)),
      access_token_(std::move(other.access_token_)),
      token_expiry_epoch_(other.token_expiry_epoch_), curl_(other.curl_) {
  other.curl_ = nullptr;
}

DriveClient &DriveClient::operator=(DriveClient &&other) noexcept {
  if (this != &other) {
    if (curl_)
      curl_easy_cleanup(static_cast<CURL *>(curl_));
    credentials_ = std::move(other.credentials_);
    access_token_ = std::move(other.access_token_);
    token_expiry_epoch_ = other.token_expiry_epoch_;
    curl_ = other.curl_;
    other.curl_ = nullptr;
  }
  return *this;
}

DriveClient::~DriveClient() {
  if (curl_)
    curl_easy_cleanup(static_cast<CURL *>(curl_));
}

// ─────────────────────────────────────────────────────────────────────────────
// Auth
// ─────────────────────────────────────────────────────────────────────────────

bool DriveClient::is_connected() const {
  // Leave a 60-second buffer before the token actually expires
  return !access_token_.empty() && now_epoch() < (token_expiry_epoch_ - 60);
}

void DriveClient::connect() {
  const std::string jwt = build_jwt();
  exchange_jwt_for_token(jwt);
}

std::string DriveClient::build_jwt() const {
  // Header
  const std::string header_json = R"({"alg":"RS256","typ":"JWT"})";
  const std::string header_b64 = base64url_encode(header_json);

  // Claims
  const long long iat = now_epoch();
  const long long exp = iat + 3600; // 1 hour

  json claims;
  claims["iss"] = credentials_.client_email;
  claims["scope"] = "https://www.googleapis.com/auth/drive";
  claims["aud"] = credentials_.token_uri;
  claims["iat"] = iat;
  claims["exp"] = exp;

  const std::string claims_b64 = base64url_encode(claims.dump());

  // Signature
  const std::string signing_input = header_b64 + "." + claims_b64;
  const std::string signature =
      rsa_sha256_sign(signing_input, credentials_.private_key);

  return signing_input + "." + signature;
}

void DriveClient::exchange_jwt_for_token(const std::string &jwt) {
  const std::string body =
      "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer"
      "&assertion=" +
      jwt;

  const std::string response = http_post_form(credentials_.token_uri, body);

  json j;
  try {
    j = json::parse(response);
  } catch (...) {
    throw AuthError("Token endpoint returned non-JSON: " + response);
  }

  if (j.contains("error")) {
    throw AuthError("Token exchange failed: " + j.value("error", "") + " — " +
                    j.value("error_description", ""));
  }

  access_token_ = j.at("access_token").get<std::string>();
  token_expiry_epoch_ = now_epoch() + j.value("expires_in", 3600);
}

void DriveClient::ensure_valid_token() {
  if (!is_connected())
    connect();
}

// ─────────────────────────────────────────────────────────────────────────────
// Drive API
// ─────────────────────────────────────────────────────────────────────────────

std::vector<DriveItem>
DriveClient::get_folder_contents(const std::string &folder_id,
                                 bool include_trashed) {
  ensure_valid_token();

  std::vector<DriveItem> all_items;
  std::string page_token;

  const std::string trashed_clause =
      include_trashed ? "" : " and trashed=false";
  const std::string q = "'" + folder_id + "' in parents" + trashed_clause;

  const std::string fields =
      "nextPageToken,files(id,name,mimeType,modifiedTime,size)";

  do {
    std::string url = "https://www.googleapis.com/drive/v3/files"
                      "?pageSize=100"
                      "&q=" +
                      std::string(curl_easy_escape(static_cast<CURL *>(curl_),
                                                   q.c_str(), 0)) +
                      "&fields=" +
                      std::string(curl_easy_escape(static_cast<CURL *>(curl_),
                                                   fields.c_str(), 0));

    if (!page_token.empty())
      url += "&pageToken=" +
             std::string(curl_easy_escape(static_cast<CURL *>(curl_),
                                          page_token.c_str(), 0));

    const std::string response = http_get(url);

    json j;
    try {
      j = json::parse(response);
    } catch (...) {
      throw ApiError(0, "Drive API returned non-JSON: " + response);
    }

    if (j.contains("error")) {
      const int code = j["error"].value("code", 0);
      const std::string msg = j["error"].value("message", "unknown error");
      throw ApiError(code,
                     "Drive API error " + std::to_string(code) + ": " + msg);
    }

    auto batch = parse_file_list(response);
    all_items.insert(all_items.end(), batch.begin(), batch.end());

    page_token = j.value("nextPageToken", "");

  } while (!page_token.empty());

  return all_items;
}

// ─────────────────────────────────────────────────────────────────────────────
// HTTP helpers
// ─────────────────────────────────────────────────────────────────────────────

std::string DriveClient::http_get(const std::string &url) {
  CURL *curl = static_cast<CURL *>(curl_);
  std::string response_body;

  curl_slist *headers = build_auth_headers();

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);

  const CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);

  if (res != CURLE_OK)
    throw DriveError(std::string("curl GET failed: ") +
                     curl_easy_strerror(res));

  return response_body;
}

std::string DriveClient::http_post_form(const std::string &url,
                                        const std::string &body) {
  CURL *curl = static_cast<CURL *>(curl_);
  std::string response_body;

  curl_slist *headers = nullptr;
  headers = curl_slist_append(
      headers, "Content-Type: application/x-www-form-urlencoded");

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

  const CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);

  if (res != CURLE_OK)
    throw DriveError(std::string("curl POST failed: ") +
                     curl_easy_strerror(res));

  return response_body;
}

curl_slist *DriveClient::build_auth_headers() const {
  curl_slist *headers = nullptr;
  const std::string auth = "Authorization: Bearer " + access_token_;
  headers = curl_slist_append(headers, auth.c_str());
  headers = curl_slist_append(headers, "Accept: application/json");
  return headers;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON parsing
// ─────────────────────────────────────────────────────────────────────────────

std::vector<DriveItem>
DriveClient::parse_file_list(const std::string &json_str) {
  const json j = json::parse(json_str);
  std::vector<DriveItem> items;

  for (const auto &f : j.value("files", json::array())) {
    DriveItem item;
    item.id = f.value("id", "");
    item.name = f.value("name", "");
    item.mime_type = f.value("mimeType", "");
    item.modified_time = f.value("modifiedTime", "");
    item.size_bytes =
        f.contains("size") ? std::stoll(f["size"].get<std::string>()) : 0LL;
    item.type = mime_to_type(item.mime_type);
    items.push_back(std::move(item));
  }

  return items;
}

} // namespace gdrive
