#include "include/detection.h"
#include "include/gpu_utils.h"
#include "include/stb_image.h"

#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

static constexpr int YUNET_W = 640;
static constexpr int YUNET_H = 640;

FaceDetector::FaceDetector() {
  if (load("./Data/face_detection_yunet_2023mar.ncnn.param",
           "./Data/face_detection_yunet_2023mar.ncnn.bin")) {
    SDL_Log("YuNet Loaded!");
  }
}

bool FaceDetector::load(const char *param, const char *bin) {
  if (net.load_param(param) != 0)
    return false;
  if (net.load_model(bin) != 0)
    return false;
  return true;
}

void FaceDetector::generate_proposals(int stride, const ncnn::Mat &cls_blob,
                                      const ncnn::Mat &obj_blob,
                                      const ncnn::Mat &bbox_blob,
                                      float prob_threshold, float scale_x,
                                      float scale_y,
                                      std::vector<FaceRect> &out) {

  const int grid_w = YUNET_W / stride;
  const int grid_h = YUNET_H / stride;
  const int num_cells = grid_w * grid_h;

  const float *cls = (const float *)cls_blob.data;
  const float *obj = (const float *)obj_blob.data;

  for (int idx = 0; idx < num_cells; idx++) {
    float cls_score = std::min(std::max(cls[idx], 0.f), 1.f);
    float obj_score = std::min(std::max(obj[idx], 0.f), 1.f);
    float score = std::sqrt(cls_score * obj_score);

    if (score < prob_threshold)
      continue;

    int grid_y = idx / grid_w;
    int grid_x = idx % grid_w;

    const float *bbox = bbox_blob.row(idx);

    float cx = (grid_x + bbox[0]) * stride;
    float cy = (grid_y + bbox[1]) * stride;
    float bw = std::exp(bbox[2]) * stride;
    float bh = std::exp(bbox[3]) * stride;

    FaceRect fr;
    fr.bounds_min =
        ImVec2((cx - bw * 0.5f) * scale_x, (cy - bh * 0.5f) * scale_y);
    fr.bounds_max =
        ImVec2((cx + bw * 0.5f) * scale_x, (cy + bh * 0.5f) * scale_y);
    fr.score = score;
    fr.count = 0;

    out.push_back(fr);
  }
}

float FaceDetector::iou_area(const FaceRect &a, const FaceRect &b) {
  float x1 = std::max(a.bounds_min.x, b.bounds_min.x);
  float y1 = std::max(a.bounds_min.y, b.bounds_min.y);
  float x2 = std::min(a.bounds_max.x, b.bounds_max.x);
  float y2 = std::min(a.bounds_max.y, b.bounds_max.y);
  float w = x2 - x1, h = y2 - y1;
  if (w <= 0 || h <= 0)
    return 0.f;
  float inter = w * h;
  float areaA =
      (a.bounds_max.x - a.bounds_min.x) * (a.bounds_max.y - a.bounds_min.y);
  float areaB =
      (b.bounds_max.x - b.bounds_min.x) * (b.bounds_max.y - b.bounds_min.y);
  return inter / (areaA + areaB - inter);
}

void FaceDetector::nms(std::vector<FaceRect> &faces, float thresh) {
  std::sort(
      faces.begin(), faces.end(),
      [](const FaceRect &a, const FaceRect &b) { return a.score > b.score; });
  std::vector<bool> suppressed(faces.size(), false);
  for (size_t i = 0; i < faces.size(); i++) {
    if (suppressed[i])
      continue;
    for (size_t j = i + 1; j < faces.size(); j++)
      if (!suppressed[j] && iou_area(faces[i], faces[j]) > thresh)
        suppressed[j] = true;
  }
  size_t dst = 0;
  for (size_t i = 0; i < faces.size(); i++)
    if (!suppressed[i])
      faces[dst++] = faces[i];
  faces.resize(dst);
}

std::vector<FaceRect>
FaceDetector::scan_faces(const std::filesystem::path &path) {
#ifdef TRACY_ENABLE
  ZoneScopedN("Detection::scan_faces");
#endif
  const std::string key = std::filesystem::weakly_canonical(path).string();
  auto it = cache.find(key);
  if (it != cache.end())
    return it->second;

  int w, h;
  unsigned char *img = load_texture_data_from_file(path, &w, &h, 0.5);
  if (!img)
    return {};

  float scale_x = (float)w / YUNET_W;
  float scale_y = (float)h / YUNET_H;

  ncnn::Mat in = ncnn::Mat::from_pixels_resize(img, ncnn::Mat::PIXEL_RGBA2BGR,
                                               w, h, YUNET_W, YUNET_H);
  stbi_image_free(img);

  const float mean[3] = {0.f, 0.f, 0.f};
  const float norm[3] = {1.f, 1.f, 1.f};
  in.substract_mean_normalize(mean, norm);

  ncnn::Extractor ex = net.create_extractor();
  ex.input("in0", in);

  ncnn::Mat cls8, cls16, cls32;
  ncnn::Mat obj8, obj16, obj32;
  ncnn::Mat bbox8, bbox16, bbox32;

  ex.extract("out0", cls8);
  ex.extract("out1", cls16);
  ex.extract("out2", cls32);
  ex.extract("out3", obj8);
  ex.extract("out4", obj16);
  ex.extract("out5", obj32);
  ex.extract("out6", bbox8);
  ex.extract("out7", bbox16);
  ex.extract("out8", bbox32);

  std::vector<FaceRect> all_results;
  all_results.reserve(128);

  generate_proposals(8, cls8, obj8, bbox8, score_thresh, scale_x, scale_y,
                     all_results);
  generate_proposals(16, cls16, obj16, bbox16, score_thresh, scale_x, scale_y,
                     all_results);
  generate_proposals(32, cls32, obj32, bbox32, score_thresh, scale_x, scale_y,
                     all_results);

  nms(all_results, nms_thresh);
  for (int i = 0; i < std::min((int)all_results.size(), 5); i++) {
    auto &f = all_results[i];
  }

  for (auto &f : all_results) {
    f.bounds_min.x = std::max(0.f, f.bounds_min.x);
    f.bounds_min.y = std::max(0.f, f.bounds_min.y);
    f.bounds_max.x = std::min((float)w, f.bounds_max.x);
    f.bounds_max.y = std::min((float)h, f.bounds_max.y);
  }

  std::sort(all_results.begin(), all_results.end(),
            [](const FaceRect &a, const FaceRect &b) {
              return a.bounds_min.x < b.bounds_min.x;
            });

  for (int i = 0; i < (int)all_results.size(); i++)
    all_results[i].count = i + 1;

  cache.emplace(key, all_results);
  return all_results;
}
