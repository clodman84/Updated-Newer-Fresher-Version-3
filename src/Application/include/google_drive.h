#pragma once

#include <curl/curl.h>
#include <filesystem>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

enum class FileType { File, Folder, Unknown };

struct DriveItem {
  std::string id;
  std::string name;
  FileType type;
  std::string mime_type;
  std::string modified_time; // RFC 3339 string e.g. "2024-01-15T10:30:00.000Z"
  long long size_bytes;      // 0 for Google Workspace files (Docs, Sheets etc.)
};

struct DriveError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct AuthError : DriveError {
  using DriveError::DriveError;
};

struct ApiError : DriveError {
  int http_status;
  ApiError(int status, const std::string &msg)
      : DriveError(msg), http_status(status) {}
};

struct ServiceAccountCredentials {
  std::string client_email;
  std::string private_key; // PEM format RSA private key
  std::string token_uri;   // usually https://oauth2.googleapis.com/token
  static ServiceAccountCredentials from_json(const std::string &json_str);
};

class DriveClient {
public:
  explicit DriveClient(ServiceAccountCredentials credentials);
  DriveClient(const DriveClient &) = delete;
  DriveClient &operator=(const DriveClient &) = delete;
  DriveClient(DriveClient &&) noexcept;
  DriveClient &operator=(DriveClient &&) noexcept;
  ~DriveClient();
  void connect();
  bool is_connected() const;
  std::vector<DriveItem> get_folder_contents(const std::string &folder_id,
                                             bool include_trashed = false);
  std::filesystem::path
  download_file(const DriveItem &item, const std::filesystem::path &dest_dir,
                std::function<void(long long bytes_done, long long bytes_total)>
                    progress_cb = {});
  void download_folder(const DriveItem &folder,
                       const std::filesystem::path &dest_dir,
                       std::function<void(int items_done, int items_total,
                                          const DriveItem &current)>
                           progress_cb = {});
  void generate_id_mapping_file(const DriveItem &folder,
                                const std::filesystem::path &dest_dir);
  std::map<std::string, std::string>
  load_id_mapping_file(const std::filesystem::path &map_file_path);

private:
  std::string build_jwt() const;
  void exchange_jwt_for_token(const std::string &jwt);
  void ensure_valid_token();
  std::string http_get(const std::string &url);
  void
  http_get_to_file(const std::string &url, const std::filesystem::path &dest,
                   std::function<void(long long, long long)> progress_cb = {});
  std::string http_post_form(const std::string &url, const std::string &body);
  static std::vector<DriveItem> parse_file_list(const std::string &json);
  curl_slist *build_auth_headers() const;
  ServiceAccountCredentials credentials_;
  std::string access_token_;
  long long token_expiry_epoch_;
  CURL *curl = nullptr;
};
