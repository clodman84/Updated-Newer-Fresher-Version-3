#pragma once

#include <stdexcept>
#include <string>
#include <vector>

// Forward declare curl to avoid including curl.h in the header
struct curl_slist;

namespace gdrive {

// ─────────────────────────────────────────────
// Data types
// ─────────────────────────────────────────────

enum class FileType { File, Folder, Unknown };

struct DriveItem {
  std::string id;
  std::string name;
  FileType type;
  std::string mime_type;
  std::string modified_time; // RFC 3339 string e.g. "2024-01-15T10:30:00.000Z"
  long long size_bytes;      // 0 for Google Workspace files (Docs, Sheets etc.)
};

// ─────────────────────────────────────────────
// Exceptions
// ─────────────────────────────────────────────

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

// ─────────────────────────────────────────────
// Service account credentials
// Loaded once from the JSON key file
// ─────────────────────────────────────────────

struct ServiceAccountCredentials {
  std::string client_email;
  std::string private_key; // PEM format RSA private key
  std::string token_uri;   // usually https://oauth2.googleapis.com/token

  // Load credentials from a service account JSON key file
  static ServiceAccountCredentials from_file(const std::string &path);
};

// ─────────────────────────────────────────────
// Main Drive client
// ─────────────────────────────────────────────

class DriveClient {
public:
  // Construct with service account credentials.
  // Does not authenticate until connect() is called.
  explicit DriveClient(ServiceAccountCredentials credentials);

  // Not copyable — owns a curl handle
  DriveClient(const DriveClient &) = delete;
  DriveClient &operator=(const DriveClient &) = delete;

  // Movable
  DriveClient(DriveClient &&) noexcept;
  DriveClient &operator=(DriveClient &&) noexcept;

  ~DriveClient();

  // ── Auth ──────────────────────────────────

  // Sign a JWT and exchange it for an access token.
  // Must be called before get_folder_contents().
  // Safe to call again when the token expires (every ~1 hour).
  void connect();

  // Returns true if we have a token and it hasn't expired yet.
  bool is_connected() const;

  // ── Drive API ─────────────────────────────

  // Returns the direct children of the given folder ID.
  // Handles pagination internally — always returns the full list.
  // Throws ApiError on non-2xx responses, AuthError if not connected.
  // If include_trashed is false (default), trashed items are excluded.
  std::vector<DriveItem> get_folder_contents(const std::string &folder_id,
                                             bool include_trashed = false);

private:
  // ── Internal auth helpers ─────────────────

  // Build and sign a JWT assertion using the service account private key
  std::string build_jwt() const;

  // POST the JWT to token_uri and store the resulting access token
  void exchange_jwt_for_token(const std::string &jwt);

  // Re-authenticate if the token has expired
  void ensure_valid_token();

  // ── Internal HTTP helpers ─────────────────

  // Perform a GET request, returning the response body as a string.
  // Attaches the current Bearer token automatically.
  std::string http_get(const std::string &url);

  // Perform a POST request with an application/x-www-form-urlencoded body.
  // Used for the token exchange — does NOT attach a Bearer token.
  std::string http_post_form(const std::string &url, const std::string &body);

  // Parse a Drive API files.list JSON response page into DriveItems
  static std::vector<DriveItem> parse_file_list(const std::string &json);

  // Build common curl headers (Content-Type, Authorization etc.)
  curl_slist *build_auth_headers() const;

  // ── State ─────────────────────────────────

  ServiceAccountCredentials credentials_;
  std::string access_token_;
  long long token_expiry_epoch_; // unix timestamp

  void *curl_; // CURL* — opaque to avoid curl.h in header
};

} // namespace gdrive
