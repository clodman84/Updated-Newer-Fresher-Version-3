#pragma once

#include "imgui.h"
#include "net.h"
#include <filesystem>
#include <unordered_map>
#include <vector>

struct FaceRect {
  ImVec2 bounds_min;
  ImVec2 bounds_max;
  float score;
  int count;
};

class FaceDetector {
public:
  FaceDetector();
  ~FaceDetector() = default;
  bool load(const char *param, const char *bin);
  std::vector<FaceRect> scan_faces(const std::filesystem::path &path);

private:
  static void generate_proposals(int stride, const ncnn::Mat &cls_blob,
                                 const ncnn::Mat &obj_blob,
                                 const ncnn::Mat &bbox_blob,
                                 float prob_threshold, float scale_x,
                                 float scale_y, std::vector<FaceRect> &out);

  static float iou_area(const FaceRect &a, const FaceRect &b);
  static void nms(std::vector<FaceRect> &faces, float thresh);

  ncnn::Net net;
  float score_thresh = 0.7f;
  float nms_thresh = 0.3f;

  std::unordered_map<std::string, std::vector<FaceRect>> cache;
};
