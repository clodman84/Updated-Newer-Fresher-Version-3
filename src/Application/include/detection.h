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
  struct Anchor {
    float cx, cy, s1, s2;
  };

  static void generate_proposals(const ncnn::Mat &anchors_mat, int feat_stride,
                                 const ncnn::Mat &score_blob,
                                 const ncnn::Mat &bbox_blob,
                                 float prob_threshold, float scale_x,
                                 float scale_y, std::vector<FaceRect> &out);

  static float iou_area(const FaceRect &a, const FaceRect &b);
  static void nms(std::vector<FaceRect> &faces, float thresh);
  ncnn::Net net;
  float score_thresh = 0.7f;
  float nms_thresh = 0.4f;

  std::unordered_map<std::string, std::vector<FaceRect>> cache;
};
