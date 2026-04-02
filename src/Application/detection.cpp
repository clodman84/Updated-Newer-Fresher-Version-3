#include "application.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect/face.hpp>

namespace {

constexpr const char *MODEL_FILENAME =
    "./Data/face_detection_yunet_2023mar.onnx";
constexpr int INFERENCE_WIDTH =
    640; // Image is scaled to this width before detection.

cv::Ptr<cv::FaceDetectorYN> g_detector;
std::unordered_map<std::string, std::vector<FaceRect>> g_cache;
std::mutex g_mutex;

cv::FaceDetectorYN &get_detector() {
  if (!g_detector) {
    const std::filesystem::path model_path =
        std::filesystem::current_path() / MODEL_FILENAME;
    if (!std::filesystem::exists(model_path))
      throw std::runtime_error("YuNet model not found at: " +
                               model_path.string());

    g_detector = cv::FaceDetectorYN::create(
        model_path.string(), "", cv::Size{INFERENCE_WIDTH, INFERENCE_WIDTH},
        0.8f, 0.3f, 5000);
  }
  return *g_detector;
}

FaceRect row_to_facerect(const float *row, float scale, int count) {
  // YuNet columns 0-3: x, y, w, h in inference-image coordinates.
  // Multiply by scale to get back to original image coordinates.
  const float x = row[0] * scale;
  const float y = row[1] * scale;
  const float w = row[2] * scale;
  const float h = row[3] * scale;
  return FaceRect{ImVec2{x, y}, ImVec2{x + w, y + h}, count};
}

} // namespace

std::vector<FaceRect> scan_faces(std::filesystem::path image_path) {
  const std::string key =
      std::filesystem::weakly_canonical(image_path).string();

  {
    std::lock_guard lock{g_mutex};
    if (auto it = g_cache.find(key); it != g_cache.end())
      return it->second;
  }

  cv::Mat img = cv::imread(key);
  if (img.empty())
    return {};

  // Scale the image down to INFERENCE_WIDTH on its longest side so that
  // YuNet sees faces at a face-to-frame ratio it was trained on.
  // Large images fed at native resolution cause faces to be "too big" and
  // get missed entirely.
  const int longest = std::max(img.cols, img.rows);
  const float scale = static_cast<float>(longest) / INFERENCE_WIDTH;

  cv::Mat inference_img;
  if (scale > 1.0f) {
    const int inf_w = static_cast<int>(img.cols / scale);
    const int inf_h = static_cast<int>(img.rows / scale);
    cv::resize(img, inference_img, cv::Size{inf_w, inf_h}, 0, 0,
               cv::INTER_AREA);
  } else {
    // Image is already small enough; avoid a pointless copy.
    inference_img = img;
  }

  cv::Mat detections;
  {
    std::lock_guard lock{g_mutex};
    auto &det = get_detector();
    det.setInputSize(cv::Size{inference_img.cols, inference_img.rows});
    det.detect(inference_img, detections);
  }

  std::vector<FaceRect> results;

  if (!detections.empty()) {
    results.reserve(static_cast<size_t>(detections.rows));
    for (int r = 0; r < detections.rows; ++r)
      results.push_back(row_to_facerect(detections.ptr<float>(r), scale, 0));

    std::sort(results.begin(), results.end(),
              [](const FaceRect &a, const FaceRect &b) {
                return a.bounds_min.x < b.bounds_min.x;
              });
    for (int i = 0; i < static_cast<int>(results.size()); ++i)
      results[i].count = i + 1;
  }

  {
    std::lock_guard lock{g_mutex};
    g_cache.emplace(key, results);
  }

  return results;
}
