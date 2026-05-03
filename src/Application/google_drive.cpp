#include "include/google_drive.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

using json = nlohmann::json;

// Libcurl write callback for storing HTTP response to a string
static size_t WriteStringCallback(void *contents, size_t size, size_t nmemb,
                                  void *userp) {
  size_t total_size = size * nmemb;
  auto *str = static_cast<std::string *>(userp);
  str->append(static_cast<char *>(contents), total_size);
  return total_size;
}

// Libcurl write callback for storing HTTP response to a file
static size_t WriteFileCallback(void *contents, size_t size, size_t nmemb,
                                void *userp) {
  size_t total_size = size * nmemb;
  auto *ofs = static_cast<std::ofstream *>(userp);
  ofs->write(static_cast<char *>(contents), total_size);
  return total_size;
}

// Libcurl progress callback
static int ProgressCallback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                            curl_off_t ultotal, curl_off_t ulnow) {
  auto *cb = static_cast<std::function<void(long long, long long)> *>(clientp);
  if (*cb && dltotal > 0) {
    (*cb)(static_cast<long long>(dlnow), static_cast<long long>(dltotal));
  }
  return 0;
}

// Base64Url encode (standard Base64 with +/ replaced by -_ and no padding)
static std::string base64url_encode(const unsigned char *buffer,
                                    size_t length) {
  BIO *bio, *b64;
  BUF_MEM *bufferPtr;
  b64 = BIO_new(BIO_f_base64());
  bio = BIO_new(BIO_s_mem());
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  BIO_push(b64, bio);
  BIO_write(b64, buffer, length);
  BIO_flush(b64);
  BIO_get_mem_ptr(b64, &bufferPtr);

  std::string encoded(bufferPtr->data, bufferPtr->length);
  BIO_free_all(b64);

  // Convert to URL safe format
  for (char &c : encoded) {
    if (c == '+')
      c = '-';
    else if (c == '/')
      c = '_';
  }
  encoded.erase(std::remove(encoded.begin(), encoded.end(), '='),
                encoded.end());
  return encoded;
}

ServiceAccountCredentials
ServiceAccountCredentials::from_file(const std::filesystem::path &path) {
  std::ifstream file(path);
  if (!file.is_open())
    throw DriveError("Could not open credentials file: " + path.string());

  json j;
  file >> j;

  ServiceAccountCredentials creds;
  creds.client_email = j.value("client_email", "");
  creds.private_key = j.value("private_key", "");
  creds.token_uri = j.value("token_uri", "https://oauth2.googleapis.com/token");

  if (creds.client_email.empty() || creds.private_key.empty()) {
    throw AuthError("Invalid credentials file format.");
  }
  return creds;
}

DriveClient::DriveClient(ServiceAccountCredentials credentials)
    : credentials_(std::move(credentials)), token_expiry_epoch_(0) {
  curl_global_init(CURL_GLOBAL_DEFAULT);
  curl = curl_easy_init();
  if (!curl)
    throw DriveError("Failed to initialize libcurl");
}

DriveClient::DriveClient(DriveClient &&other) noexcept
    : credentials_(std::move(other.credentials_)),
      access_token_(std::move(other.access_token_)),
      token_expiry_epoch_(other.token_expiry_epoch_), curl(other.curl) {
  other.curl = nullptr;
}

DriveClient &DriveClient::operator=(DriveClient &&other) noexcept {
  if (this != &other) {
    if (curl)
      curl_easy_cleanup(curl);
    credentials_ = std::move(other.credentials_);
    access_token_ = std::move(other.access_token_);
    token_expiry_epoch_ = other.token_expiry_epoch_;
    curl = other.curl;
    other.curl = nullptr;
  }
  return *this;
}

DriveClient::~DriveClient() {
  if (curl) {
    curl_easy_cleanup(curl);
  }
  curl_global_cleanup();
}

void DriveClient::connect() { ensure_valid_token(); }

bool DriveClient::is_connected() const {
  auto now = std::chrono::duration_cast<std::chrono::seconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();
  return !access_token_.empty() && now < token_expiry_epoch_;
}

void DriveClient::ensure_valid_token() {
  if (!is_connected()) {
    std::string jwt = build_jwt();
    exchange_jwt_for_token(jwt);
  }
}

std::string DriveClient::build_jwt() const {
  auto now = std::chrono::duration_cast<std::chrono::seconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();

  json header = {{"alg", "RS256"}, {"typ", "JWT"}};
  json claim = {{"iss", credentials_.client_email},
                {"scope", "https://www.googleapis.com/auth/drive"},
                {"aud", credentials_.token_uri},
                {"exp", now + 3600},
                {"iat", now}};

  std::string header_b64 = base64url_encode(
      reinterpret_cast<const unsigned char *>(header.dump().c_str()),
      header.dump().length());
  std::string claim_b64 = base64url_encode(
      reinterpret_cast<const unsigned char *>(claim.dump().c_str()),
      claim.dump().length());
  std::string sign_input = header_b64 + "." + claim_b64;

  // Sign using OpenSSL RSA-SHA256
  BIO *key_bio = BIO_new_mem_buf(credentials_.private_key.c_str(), -1);
  EVP_PKEY *pkey = PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr);
  BIO_free(key_bio);
  if (!pkey)
    throw AuthError("Failed to load private key");

  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey);
  EVP_DigestSignUpdate(ctx, sign_input.c_str(), sign_input.length());

  size_t sig_len;
  EVP_DigestSignFinal(ctx, nullptr, &sig_len);
  std::vector<unsigned char> signature(sig_len);
  EVP_DigestSignFinal(ctx, signature.data(), &sig_len);

  EVP_MD_CTX_free(ctx);
  EVP_PKEY_free(pkey);

  std::string sig_b64 = base64url_encode(signature.data(), sig_len);
  return sign_input + "." + sig_b64;
}

void DriveClient::exchange_jwt_for_token(const std::string &jwt) {
  std::string body =
      "grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer&assertion=" + jwt;
  std::string response = http_post_form(credentials_.token_uri, body);

  try {
    json j = json::parse(response);
    if (j.contains("error")) {
      throw AuthError("Failed to authenticate: " +
                      j["error_description"].get<std::string>());
    }
    access_token_ = j["access_token"];
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    token_expiry_epoch_ = now + j["expires_in"].get<int>() - 60; // 60s buffer
  } catch (const json::exception &e) {
    throw AuthError("Failed to parse token response");
  }
}

curl_slist *DriveClient::build_auth_headers() const {
  curl_slist *headers = nullptr;
  std::string auth_header = "Authorization: Bearer " + access_token_;
  headers = curl_slist_append(headers, auth_header.c_str());
  return headers;
}

std::string DriveClient::http_get(const std::string &url) {
  ensure_valid_token();
  curl_easy_reset(curl);

  std::string response;
  curl_slist *headers = build_auth_headers();

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStringCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

  CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);

  if (res != CURLE_OK)
    throw ApiError(0, curl_easy_strerror(res));

  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  if (http_code >= 400)
    throw ApiError(http_code, "HTTP GET failed: " + response);

  return response;
}

std::string DriveClient::http_post_form(const std::string &url,
                                        const std::string &body) {
  curl_easy_reset(curl);
  std::string response;

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStringCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK)
    throw ApiError(0, curl_easy_strerror(res));

  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  if (http_code >= 400)
    throw ApiError(http_code, "HTTP POST failed: " + response);

  return response;
}

void DriveClient::http_get_to_file(
    const std::string &url, const std::filesystem::path &dest,
    std::function<void(long long, long long)> progress_cb) {
  ensure_valid_token();
  curl_easy_reset(curl);

  std::ofstream ofs(dest, std::ios::binary);
  if (!ofs.is_open())
    throw DriveError("Cannot open destination file");

  curl_slist *headers = build_auth_headers();
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ofs);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,
                   1L); // Follow redirects for downloads

  if (progress_cb) {
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress_cb);
  }

  CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  ofs.close();

  if (res != CURLE_OK)
    throw ApiError(0, curl_easy_strerror(res));

  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  if (http_code >= 400)
    throw ApiError(http_code, "File download failed");
}

std::vector<DriveItem>
DriveClient::parse_file_list(const std::string &json_str) {
  std::vector<DriveItem> items;
  auto j = json::parse(json_str);

  if (j.contains("files")) {
    for (const auto &file : j["files"]) {
      DriveItem item;
      item.id = file.value("id", "");
      item.name = file.value("name", "");
      item.mime_type = file.value("mimeType", "");
      item.modified_time = file.value("modifiedTime", "");

      // Standard Drive folders use this MIME type
      if (item.mime_type == "application/vnd.google-apps.folder") {
        item.type = FileType::Folder;
        item.size_bytes = 0;
      } else {
        item.type = FileType::File;
        // Google Docs/Sheets don't have a 'size' attribute in the same way,
        // handled gracefully
        std::string size_str = file.value("size", "0");
        item.size_bytes = std::stoll(size_str);
      }
      items.push_back(item);
    }
  }
  return items;
}

std::vector<DriveItem>
DriveClient::get_folder_contents(const std::string &folder_id,
                                 bool include_trashed) {
  std::vector<DriveItem> all_items;

  std::string query = "'" + folder_id + "' in parents";
  if (!include_trashed)
    query += " and trashed = false";

  char *encoded_q = curl_easy_escape(curl, query.c_str(), query.length());
  // Added nextPageToken to the requested fields
  std::string base_url =
      "https://www.googleapis.com/drive/v3/files?q=" + std::string(encoded_q) +
      "&fields=nextPageToken,files(id,name,mimeType,modifiedTime,size)";
  curl_free(encoded_q);

  std::string page_token = "";

  // Loop to handle API Pagination
  do {
    std::string url = base_url;
    if (!page_token.empty()) {
      url += "&pageToken=" + page_token;
    }

    std::string response = http_get(url);
    auto j = json::parse(response);

    if (j.contains("files")) {
      for (const auto &file : j["files"]) {
        DriveItem item;
        item.id = file.value("id", "");
        item.name = file.value("name", "");
        item.mime_type = file.value("mimeType", "");
        item.modified_time = file.value("modifiedTime", "");

        if (item.mime_type == "application/vnd.google-apps.folder") {
          item.type = FileType::Folder;
          item.size_bytes = 0;
        } else {
          item.type = FileType::File;
          std::string size_str = file.value("size", "0");
          item.size_bytes = std::stoll(size_str);
        }
        all_items.push_back(item);
      }
    }

    // Grab the token for the next page, if it exists
    page_token = j.value("nextPageToken", "");

  } while (!page_token.empty());

  return all_items;
}

void DriveClient::download_folder(
    const DriveItem &folder, const std::filesystem::path &dest_dir,
    std::function<void(int, int, const DriveItem &)> progress_cb) {
  if (folder.type != FileType::Folder) {
    throw DriveError("Attempted to use download_folder on a file.");
  }

  struct DownloadTask {
    DriveItem item;
    std::filesystem::path dest;
  };
  std::vector<DownloadTask> tasks;

  // 1. Pre-flight pass: Gather all files
  std::function<void(const DriveItem &, const std::filesystem::path &)>
      gather_tasks = [&](const DriveItem &current_folder,
                         const std::filesystem::path &current_dest) {
        std::filesystem::create_directories(current_dest);
        auto items = get_folder_contents(current_folder.id);
        for (const auto &item : items) {
          if (item.type == FileType::Folder) {
            gather_tasks(item, current_dest / item.name); // Recurse
          } else {
            tasks.push_back({item, current_dest});
          }
        }
      };

  if (progress_cb)
    progress_cb(0, 0, folder);
  gather_tasks(folder, dest_dir / folder.name);

  int total_files = tasks.size();
  if (total_files == 0)
    return;

  ensure_valid_token();
  std::string current_token = access_token_;

  // 2. Concurrent Download Phase using curl_multi
  CURLM *multi_handle = curl_multi_init();
  if (!multi_handle)
    throw DriveError("Failed to initialize curl_multi");

  // We need to keep the file streams and headers alive for the duration of
  // their specific transfer
  struct TransferData {
    DriveItem item;
    std::ofstream ofs;
    curl_slist *headers = nullptr;
  };

  std::map<CURL *, std::unique_ptr<TransferData>> active_transfers;
  int task_idx = 0;
  int files_done = 0;
  int still_running = 0;
  const int MAX_CONCURRENT = 8;

  // The multi loop
  do {
    // Fill the queue up to the maximum concurrent connections limit
    while (active_transfers.size() < MAX_CONCURRENT && task_idx < total_files) {
      const auto &task = tasks[task_idx++];
      std::filesystem::path dest_file = task.dest / task.item.name;

      auto tdata = std::make_unique<TransferData>();
      tdata->item = task.item;
      tdata->ofs.open(dest_file, std::ios::binary);

      if (!tdata->ofs.is_open())
        continue;

      std::string url = "https://www.googleapis.com/drive/v3/files/" +
                        task.item.id + "?alt=media";
      std::string auth_header = "Authorization: Bearer " + current_token;
      tdata->headers = curl_slist_append(nullptr, auth_header.c_str());

      CURL *easy = curl_easy_init();
      curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
      curl_easy_setopt(easy, CURLOPT_HTTPHEADER, tdata->headers);
      curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, WriteFileCallback);
      curl_easy_setopt(easy, CURLOPT_WRITEDATA, &tdata->ofs);
      curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);

      curl_multi_add_handle(multi_handle, easy);
      active_transfers[easy] = std::move(tdata);
    }

    // Perform the network transfers for all active handles
    curl_multi_perform(multi_handle, &still_running);

    // Check if any specific transfers just finished
    int msgs_left;
    CURLMsg *msg;
    while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
      if (msg->msg == CURLMSG_DONE) {
        CURL *easy = msg->easy_handle;
        auto it = active_transfers.find(easy);

        if (it != active_transfers.end()) {
          it->second->ofs.close();
          curl_slist_free_all(it->second->headers);

          files_done++;
          if (progress_cb)
            progress_cb(files_done, total_files, it->second->item);

          active_transfers.erase(it);
        }

        // Cleanup the handle
        curl_multi_remove_handle(multi_handle, easy);
        curl_easy_cleanup(easy);
      }
    }

    // Wait for activity on the sockets so we don't spam the CPU in a tight loop
    if (still_running || task_idx < total_files) {
      curl_multi_poll(multi_handle, nullptr, 0, 1000, nullptr);
    }

  } while (still_running > 0 || task_idx < total_files);

  curl_multi_cleanup(multi_handle);
}

std::filesystem::path DriveClient::download_file(
    const DriveItem &item, const std::filesystem::path &dest_dir,
    std::function<void(long long, long long)> progress_cb) {
  if (item.type == FileType::Folder) {
    throw DriveError("Attempted to use download_file on a folder.");
  }

  std::filesystem::create_directories(dest_dir);
  std::filesystem::path dest_file = dest_dir / item.name;

  std::string url =
      "https://www.googleapis.com/drive/v3/files/" + item.id + "?alt=media";
  http_get_to_file(url, dest_file, progress_cb);

  return dest_file;
}
