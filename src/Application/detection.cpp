#include "detection.h"
#include "gpu_utils.h"
#include "stb_image.h"

#include <algorithm>
#include <cmath>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

FaceDetector::FaceDetector() {
  if (load("./Data/mnet.25-opt.param", "./Data/mnet.25-opt.bin")) {
    printf("RetinaFace loaded successfully\n");
  }
}

bool FaceDetector::load(const char *param, const char *bin) {
  if (net.load_param(param) != 0)
    return false;
  if (net.load_model(bin) != 0)
    return false;
  return true;
}

static ncnn::Mat generate_anchors(int base_size, float scale1, float scale2) {
  ncnn::Mat anchors(4, 2);

  const float cx = base_size * 0.5f;
  const float cy = base_size * 0.5f;

  const float scales[2] = {scale1, scale2};

  for (int i = 0; i < 2; i++) {
    float s = scales[i] * base_size;

    float *anchor = anchors.row(i);
    anchor[0] = cx - s * 0.5f;
    anchor[1] = cy - s * 0.5f;
    anchor[2] = cx + s * 0.5f;
    anchor[3] = cy + s * 0.5f;
  }

  return anchors;
}

void FaceDetector::generate_proposals(const ncnn::Mat &anchors, int stride,
                                      const ncnn::Mat &score_blob,
                                      const ncnn::Mat &bbox_blob,
                                      float prob_threshold, float scale_x,
                                      float scale_y,
                                      std::vector<FaceRect> &out) {
#ifdef TRACY_ENABLE
  ZoneScopedN("Detection::generate_proposals");
#endif

  const int w = score_blob.w;
  const int h = score_blob.h;
  const int num_anchors = anchors.h;

  for (int q = 0; q < num_anchors; q++) {
    const float *anchor = anchors.row(q);

    const ncnn::Mat score = score_blob.channel(q + num_anchors);

    const ncnn::Mat dx = bbox_blob.channel(q * 4 + 0);
    const ncnn::Mat dy = bbox_blob.channel(q * 4 + 1);
    const ncnn::Mat dw = bbox_blob.channel(q * 4 + 2);
    const ncnn::Mat dh = bbox_blob.channel(q * 4 + 3);

    float anchor_w = anchor[2] - anchor[0];
    float anchor_h = anchor[3] - anchor[1];

    float anchor_y = anchor[1];

    for (int i = 0; i < h; i++) {
      float anchor_x = anchor[0];

      for (int j = 0; j < w; j++) {
        int idx = i * w + j;

        float prob = score[idx];
        if (prob < prob_threshold) {
          anchor_x += stride;
          continue;
        }

        float cx = anchor_x + anchor_w * 0.5f;
        float cy = anchor_y + anchor_h * 0.5f;

        float pb_cx = cx + anchor_w * dx[idx];
        float pb_cy = cy + anchor_h * dy[idx];
        float pb_w = anchor_w * std::exp(dw[idx]);
        float pb_h = anchor_h * std::exp(dh[idx]);

        float x0 = pb_cx - pb_w * 0.5f;
        float y0 = pb_cy - pb_h * 0.5f;
        float x1 = pb_cx + pb_w * 0.5f;
        float y1 = pb_cy + pb_h * 0.5f;

        FaceRect fr;
        fr.bounds_min = ImVec2(x0 * scale_x, y0 * scale_y);
        fr.bounds_max = ImVec2(x1 * scale_x, y1 * scale_y);
        fr.score = prob;
        fr.count = 0;

        out.push_back(fr);

        anchor_x += stride;
      }

      anchor_y += stride;
    }
  }
}

float FaceDetector::iou_area(const FaceRect &a, const FaceRect &b) {
  float x1 = std::max(a.bounds_min.x, b.bounds_min.x);
  float y1 = std::max(a.bounds_min.y, b.bounds_min.y);
  float x2 = std::min(a.bounds_max.x, b.bounds_max.x);
  float y2 = std::min(a.bounds_max.y, b.bounds_max.y);

  float w = x2 - x1;
  float h = y2 - y1;

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

    for (size_t j = i + 1; j < faces.size(); j++) {
      if (!suppressed[j] && iou_area(faces[i], faces[j]) > thresh) {
        suppressed[j] = true;
      }
    }
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

  // ---- load image ----
  int w, h;
  unsigned char *img = load_texture_data_from_file(path, &w, &h);
  if (!img)
    return {};

  std::vector<FaceRect> all_results;
  all_results.reserve(128);

  auto run_scale = [&](float scale_factor) {
    int scaled_w = int(w * scale_factor);
    int scaled_h = int(h * scale_factor);

    // limit max input size (avoid huge tensors)
    const int target_size = 1400;
    float resize_scale = (float)target_size / std::max(scaled_w, scaled_h);

    if (resize_scale > 1.f)
      return; // shut the fuck up dawg

    int input_w = int(scaled_w * resize_scale);
    int input_h = int(scaled_h * resize_scale);

    float scale_x = (float)w / input_w;
    float scale_y = (float)h / input_h;

    ncnn::Mat in = ncnn::Mat::from_pixels_resize(img, ncnn::Mat::PIXEL_RGBA2BGR,
                                                 w, h, input_w, input_h);

    const float mean[3] = {104.f, 117.f, 123.f};
    in.substract_mean_normalize(mean, nullptr);

    ncnn::Extractor ex = net.create_extractor();
    ex.input("data", in);

    ncnn::Mat score32, bbox32;
    ncnn::Mat score16, bbox16;
    ncnn::Mat score8, bbox8;

    ex.extract("face_rpn_cls_prob_reshape_stride32", score32);
    ex.extract("face_rpn_bbox_pred_stride32", bbox32);
    ex.extract("face_rpn_cls_prob_reshape_stride16", score16);
    ex.extract("face_rpn_bbox_pred_stride16", bbox16);
    ex.extract("face_rpn_cls_prob_reshape_stride8", score8);
    ex.extract("face_rpn_bbox_pred_stride8", bbox8);

    // temp results for this scale
    std::vector<FaceRect> tmp;

    // stride 32
    {
      auto anchors = generate_anchors(16, 32.f, 16.f);
      generate_proposals(anchors, 32, score32, bbox32, score_thresh, scale_x,
                         scale_y, tmp);
    }

    // stride 16
    {
      auto anchors = generate_anchors(16, 8.f, 4.f);
      generate_proposals(anchors, 16, score16, bbox16, score_thresh, scale_x,
                         scale_y, tmp);
    }

    // stride 8
    {
      auto anchors = generate_anchors(16, 2.f, 1.f);
      generate_proposals(anchors, 8, score8, bbox8, score_thresh, scale_x,
                         scale_y, tmp);
    }

    // append
    all_results.insert(all_results.end(), tmp.begin(), tmp.end());
  };

  // ----------------------------------------
  // run multi-scale
  // ----------------------------------------

  run_scale(1.0f); // normal
  run_scale(0.5f); // helps large faces

  stbi_image_free(img);

  // ----------------------------------------
  // final NMS
  // ----------------------------------------

  nms(all_results, nms_thresh);

  for (auto &f : all_results) {
    f.bounds_min.x = std::max(0.f, f.bounds_min.x);
    f.bounds_min.y = std::max(0.f, f.bounds_min.y);
    f.bounds_max.x = std::min((float)w, f.bounds_max.x);
    f.bounds_max.y = std::min((float)h, f.bounds_max.y);
  }

  // ----------------------------------------
  // sort left → right
  // ----------------------------------------
  std::sort(all_results.begin(), all_results.end(),
            [](const FaceRect &a, const FaceRect &b) {
              return a.bounds_min.x < b.bounds_min.x;
            });

  for (int i = 0; i < (int)all_results.size(); i++)
    all_results[i].count = i + 1;

  cache.emplace(key, all_results);

  return all_results;
}
