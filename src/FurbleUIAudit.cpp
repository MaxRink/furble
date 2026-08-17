#include "FurbleUIAudit.h"

#if defined(FURBLE_SIM) || defined(FURBLE_CONSOLE)

#include <cstdio>
#include <string>

namespace Furble {
namespace UIAudit {

namespace {

struct stats_t {
  uint32_t visibleObjects = 0;
  uint32_t labels = 0;
  uint32_t issues = 0;
  uint32_t overlaps = 0;
  uint32_t clipped = 0;
};

void printJsonString(const char *text) {
  putchar('"');
  if (text != nullptr) {
    for (const auto *cursor = reinterpret_cast<const unsigned char *>(text); *cursor != '\0';
         cursor++) {
      switch (*cursor) {
        case '"':
          printf("\\\"");
          break;
        case '\\':
          printf("\\\\");
          break;
        case '\b':
          printf("\\b");
          break;
        case '\f':
          printf("\\f");
          break;
        case '\n':
          printf("\\n");
          break;
        case '\r':
          printf("\\r");
          break;
        case '\t':
          printf("\\t");
          break;
        default:
          if (*cursor < 0x20) {
            printf("\\u%04x", static_cast<unsigned>(*cursor));
          } else {
            putchar(*cursor);
          }
          break;
      }
    }
  }
  putchar('"');
}

void printRect(const lv_area_t &area) {
  printf("[%ld,%ld,%ld,%ld]", static_cast<long>(area.x1), static_cast<long>(area.y1),
         static_cast<long>(area.x2), static_cast<long>(area.y2));
}

bool visible(const lv_obj_t *obj) {
  for (const lv_obj_t *current = obj; current != nullptr; current = lv_obj_get_parent(current)) {
    if (lv_obj_has_flag(current, LV_OBJ_FLAG_HIDDEN)) {
      return false;
    }
  }
  return true;
}

bool overlaps(const lv_area_t &first, const lv_area_t &second) {
  return (first.x1 <= second.x2) && (second.x1 <= first.x2) && (first.y1 <= second.y2)
         && (second.y1 <= first.y2);
}

bool empty(const lv_area_t &area) {
  return (area.x1 > area.x2) || (area.y1 > area.y2);
}

void printOverlap(const lv_obj_t *label,
                  const std::string &labelPath,
                  const lv_area_t &labelArea,
                  const std::string &siblingPath,
                  const lv_area_t &siblingArea,
                  stats_t &stats) {
  stats.issues++;
  stats.overlaps++;

  printf("{\"type\":\"overlap\",\"label_path\":");
  printJsonString(labelPath.c_str());
  printf(",\"sibling_path\":");
  printJsonString(siblingPath.c_str());
  printf(",\"label_rect\":");
  printRect(labelArea);
  printf(",\"sibling_rect\":");
  printRect(siblingArea);
  printf(",\"text\":");
  printJsonString(lv_label_get_text(label));
  printf("}\n");
}

void auditLabel(lv_obj_t *label,
                const std::string &labelPath,
                lv_obj_t *parent,
                uint32_t labelIndex,
                stats_t &stats) {
  lv_area_t labelArea;
  lv_obj_get_coords(label, &labelArea);

  const char *text = lv_label_get_text(label);
  if (text == nullptr) {
    text = "";
  }
  const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  if (font == nullptr) {
    font = LV_FONT_DEFAULT;
  }

  const int32_t letterSpace = lv_obj_get_style_text_letter_space(label, LV_PART_MAIN);
  const int32_t lineSpace = lv_obj_get_style_text_line_space(label, LV_PART_MAIN);
  const lv_label_long_mode_t longMode = lv_label_get_long_mode(label);

  // Scrolling and dot labels overflow by design, they are not clipping issues.
  const bool intentionalOverflow = (longMode == LV_LABEL_LONG_MODE_SCROLL)
                                   || (longMode == LV_LABEL_LONG_MODE_SCROLL_CIRCULAR)
                                   || (longMode == LV_LABEL_LONG_MODE_DOTS);

  if ((text[0] != '\0') && !intentionalOverflow) {
    const int32_t contentWidth = lv_obj_get_content_width(label);
    lv_point_t textSize = {0, 0};
    if (longMode == LV_LABEL_LONG_MODE_WRAP) {
      // Wrapped labels are multi-line, clipping shows up as excess height.
      lv_text_get_size(&textSize, text, font, letterSpace, lineSpace, contentWidth,
                       LV_TEXT_FLAG_NONE);
      const int32_t contentHeight = lv_obj_get_content_height(label);
      if (textSize.y > contentHeight) {
        stats.issues++;
        stats.clipped++;
        printf("{\"type\":\"clipped\",\"label_path\":");
        printJsonString(labelPath.c_str());
        printf(",\"label_rect\":");
        printRect(labelArea);
        printf(",\"label_height\":%ld,\"text_height\":%ld,\"text\":",
               static_cast<long>(contentHeight), static_cast<long>(textSize.y));
        printJsonString(text);
        printf("}\n");
      }
    } else {
      lv_text_get_size(&textSize, text, font, letterSpace, lineSpace, LV_COORD_MAX,
                       LV_TEXT_FLAG_EXPAND);
      if (textSize.x > contentWidth) {
        stats.issues++;
        stats.clipped++;
        printf("{\"type\":\"clipped\",\"label_path\":");
        printJsonString(labelPath.c_str());
        printf(",\"label_rect\":");
        printRect(labelArea);
        printf(",\"label_width\":%ld,\"text_width\":%ld,\"text\":", static_cast<long>(contentWidth),
               static_cast<long>(textSize.x));
        printJsonString(text);
        printf("}\n");
      }
    }
  }

  if (parent == nullptr) {
    return;
  }

  const auto slash = labelPath.rfind('/');
  const std::string parentPath = labelPath.substr(0, slash);
  const uint32_t childCount = lv_obj_get_child_count(parent);
  for (uint32_t index = 0; index < childCount; index++) {
    lv_obj_t *sibling = lv_obj_get_child(parent, static_cast<int32_t>(index));
    if ((sibling == label) || !visible(sibling)) {
      continue;
    }

    lv_area_t siblingArea;
    lv_obj_get_coords(sibling, &siblingArea);
    if (empty(siblingArea)) {
      continue;
    }

    if (lv_obj_check_type(sibling, &lv_label_class) && (index < labelIndex)) {
      continue;
    }

    if (overlaps(labelArea, siblingArea)) {
      printOverlap(label, labelPath, labelArea, parentPath + "/" + std::to_string(index),
                   siblingArea, stats);
    }
  }
}

void walk(lv_obj_t *obj,
          const std::string &path,
          lv_obj_t *parent,
          uint32_t childIndex,
          stats_t &stats) {
  if (!visible(obj)) {
    return;
  }

  stats.visibleObjects++;
  if (lv_obj_check_type(obj, &lv_label_class)) {
    stats.labels++;
    auditLabel(obj, path, parent, childIndex, stats);
  }

  const uint32_t childCount = lv_obj_get_child_count(obj);
  for (uint32_t index = 0; index < childCount; index++) {
    lv_obj_t *child = lv_obj_get_child(obj, static_cast<int32_t>(index));
    walk(child, path + "/" + std::to_string(index), obj, index, stats);
  }
}

}  // namespace

void dump(lv_obj_t *root) {
  if (root == nullptr) {
    printf(
        "{\"type\":\"error\",\"schema\":\"furble-ui-audit/v1\","
        "\"message\":\"no active screen\"}\n");
    return;
  }

  lv_obj_update_layout(root);
  stats_t stats;
  printf(
      "{\"type\":\"begin\",\"schema\":\"furble-ui-audit/v1\","
      "\"screen\":\"active\"}\n");
  walk(root, "root", nullptr, 0, stats);
  printf(
      "{\"type\":\"end\",\"visible_objects\":%lu,\"labels\":%lu,\"issues\":%lu,"
      "\"overlaps\":%lu,\"clipped\":%lu}\n",
      static_cast<unsigned long>(stats.visibleObjects), static_cast<unsigned long>(stats.labels),
      static_cast<unsigned long>(stats.issues), static_cast<unsigned long>(stats.overlaps),
      static_cast<unsigned long>(stats.clipped));
}

}  // namespace UIAudit
}  // namespace Furble

#endif
